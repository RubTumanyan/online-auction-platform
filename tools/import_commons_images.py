#!/usr/bin/env python3
"""One-time Commons photo import. No network code is used by the C++ application."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timedelta, timezone
import hashlib
from html import unescape
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import warnings

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
API = "https://commons.wikimedia.org/w/api.php"
USER_AGENT = "OnlineAuctionPhase2Importer/1.0 (educational; Wikimedia Commons local photo import)"
Image.MAX_IMAGE_PIXELS = 24_000_000
warnings.simplefilter("error", Image.DecompressionBombWarning)
BAD = re.compile(r"\b(diagram|drawing|sketch|logo|screenshot|map|rendering|render|svg|icon|ai.generated|midjourney|stable.diffusion|portrait|wearing|selfie|fashion models|coat.of.arms|heraldry|illustrated|engraving|lithograph|woodcut|dust.jacket|trailer|facade|panoramio|crimson.sweater|Google Art Project|insects|flies|advertisement|poster)\b", re.I)
CREDIT_FIELDS = ["local_path", "file_page_url", "author", "author_url", "license", "license_url", "category", "modifications"]


class PlainText(HTMLParser):
    def __init__(self):
        super().__init__()
        self.parts = []
        self.links = []

    def handle_data(self, data):
        self.parts.append(data)

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            for key, value in attrs:
                if key == "href" and value and value.startswith("https://"):
                    self.links.append(value)


def plain(value):
    parser = PlainText()
    parser.feed(value)
    return re.sub(r"\s+", " ", unescape(" ".join(parser.parts))).strip()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def replace_file(temp, path):
    for attempt in range(20):
        try:
            temp.replace(path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.1 * (attempt + 1))


def atomic_json(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    replace_file(temp, path)


def fetch(url, image=False):
    parsed = urllib.parse.urlparse(url)
    allowed = {"commons.wikimedia.org", "upload.wikimedia.org", "thumb.wikimedia.org"} if image else {"commons.wikimedia.org"}
    if parsed.scheme != "https" or parsed.hostname not in allowed:
        raise ValueError("Refusing a non-Wikimedia URL")
    for attempt in range(4):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=35) as response:
                if urllib.parse.urlparse(response.url).hostname not in allowed:
                    raise ValueError("Unexpected redirect host")
                mime = response.headers.get_content_type()
                if image and mime not in {"image/jpeg", "image/png"}:
                    raise ValueError(f"Non-JPEG/PNG response: {mime}")
                limit = 12_000_000 if image else 30_000_000
                data = response.read(limit + 1)
                if len(data) > limit:
                    raise ValueError("Response exceeds size limit")
                return data
        except urllib.error.HTTPError as error:
            if error.code not in {429, 500, 502, 503, 504} or attempt == 3:
                raise
            time.sleep(min(60, int(error.headers.get("Retry-After", 2 ** (attempt + 1)))))
        except (urllib.error.URLError, TimeoutError):
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("Request retries exhausted")


def category_batches(name):
    # Category membership is semantic. Plain title searches can return e.g.
    # railway turntables instead of record players, or coats of arms instead of coats.
    queue = [(name, 0)]
    visited = set()
    while queue and len(visited) < 20:
        category, depth = queue.pop(0)
        if category in visited:
            continue
        visited.add(category)
        continuation = {}
        while True:
            parameters = dict(action="query", format="json", formatversion=2,
                              generator="categorymembers", gcmtitle="Category:" + category,
                              gcmtype="file", gcmlimit=25, prop="imageinfo",
                              iiprop="url|extmetadata|metadata|mime|sha1|size|mediatype",
                              maxlag=5, **continuation)
            payload = json.loads(fetch(API + "?" + urllib.parse.urlencode(parameters)))
            if "error" in payload:
                raise RuntimeError(f'Commons API error: {payload["error"]}')
            yield decode_candidates(payload)
            continuation = payload.get("continue", {})
            if not continuation:
                break
        if depth < 2:
            parameters = dict(action="query", format="json", formatversion=2,
                              list="categorymembers", cmtitle="Category:" + category,
                              cmtype="subcat", cmlimit=100)
            payload = json.loads(fetch(API + "?" + urllib.parse.urlencode(parameters)))
            for child in payload.get("query", {}).get("categorymembers", []):
                title = child["title"].removeprefix("Category:")
                if not BAD.search(title) and not re.search(
                    r"taken with|taken by|photographed with|people|men |women |children|in art|paintings of|drawings of|logos|advertisements|manuals|interiors|exteriors|disassembled", title, re.I):
                    queue.append((title, depth + 1))


def search_batches(term):
    """Fall back to Commons full-text search when a category tree is too sparse."""
    continuation = {}
    for _ in range(12):
        parameters = dict(action="query", format="json", formatversion=2,
                          generator="search", gsrsearch=term, gsrnamespace=6, gsrlimit=25,
                          prop="imageinfo",
                          iiprop="url|extmetadata|metadata|mime|sha1|size|mediatype",
                          maxlag=5, **continuation)
        payload = json.loads(fetch(API + "?" + urllib.parse.urlencode(parameters)))
        if "error" in payload:
            raise RuntimeError(f'Commons API error: {payload["error"]}')
        yield decode_candidates(payload)
        continuation = payload.get("continue", {})
        if not continuation:
            break


def decode_candidates(payload):
    found = []
    for page in payload.get("query", {}).get("pages", []):
        if not page.get("imageinfo"):
            continue
        info = page["imageinfo"][0]
        metadata = info.get("extmetadata", {})
        value = lambda key: metadata.get(key, {}).get("value", "")
        title = re.sub(r"\.(jpe?g|png)$", "", page["title"].removeprefix("File:"), flags=re.I).replace("_", " ")
        description = plain(value("ImageDescription"))
        if info.get("mime") not in {"image/jpeg", "image/png"} or info.get("mediatype") != "BITMAP":
            continue
        camera = {entry.get("name"): entry.get("value") for entry in info.get("metadata", [])}
        if not any(camera.get(key) for key in ["Make", "Model", "CameraModelName"]):
            continue  # Require camera-origin evidence; do not treat scans/graphics as photos.
        if BAD.search(title) or BAD.search(plain(value("Categories"))):
            continue
        if len(title) < 8 or len(re.findall(r"[A-Za-z]", title)) < 5:
            continue
        license_name = plain(value("LicenseShortName"))
        license_code = plain(value("License")).lower()
        is_free = license_code in {"cc-zero", "pd", "pdm"} or license_name.lower() in {"public domain", "cc0", "cc0 1.0"}
        if not is_free and not re.fullmatch(r"cc-by(?:-sa)?-\d\.\d", license_code):
            continue  # No NC, ND, GFDL-only, unknown, or custom licenses.
        author = plain(value("Artist"))
        if not author or not info.get("sha1") or not info.get("url"):
            continue
        author_parser = PlainText()
        author_parser.feed(value("Artist"))
        license_url = unescape(value("LicenseUrl"))
        if license_url.startswith("//"):
            license_url = "https:" + license_url
        if not license_url and is_free:
            license_url = "https://creativecommons.org/publicdomain/mark/1.0/"
        if not license_url.startswith("https://"):
            continue
        filename = page["title"].removeprefix("File:")
        thumbnail = ("https://commons.wikimedia.org/wiki/Special:Redirect/file/" +
                     urllib.parse.quote(filename, safe="") + "?width=480")
        found.append(dict(file_page_url=info["descriptionurl"], original_url=info["url"],
                          source_sha1=info["sha1"], thumbnail_url=thumbnail,
                          author=author, author_url="; ".join(author_parser.links),
                          license=license_name, license_url=license_url,
                          source_title=title, source_description=description[:1800],
                          camera_model=str(camera.get("Model", camera.get("Make", ""))), public_domain=is_free))
    # Prefer PD/CC0 within each search batch, then stable Commons title order.
    return sorted(found, key=lambda item: (not item["public_domain"], item["source_title"]))


def photograph(candidate):
    try:
        raw = fetch(candidate["thumbnail_url"], image=True)
        with Image.open(io.BytesIO(raw)) as probe:
            if probe.format not in {"JPEG", "PNG"} or getattr(probe, "n_frames", 1) != 1:
                raise ValueError("Not a static JPEG or PNG")
            probe.verify()
        with Image.open(io.BytesIO(raw)) as original:
            image = ImageOps.exif_transpose(original).convert("RGBA")
            if image.width < 60 or image.height < 60:
                raise ValueError("Thumbnail too small")
            image.thumbnail((480, 320), Image.Resampling.LANCZOS)
            background = Image.new("RGB", image.size, "white")
            background.paste(image, mask=image.getchannel("A"))
            pixels = hashlib.sha256(str(background.size).encode() + background.tobytes()).hexdigest()
            output = io.BytesIO()
            background.save(output, "JPEG", quality=88, optimize=True)
            data = output.getvalue()
            return candidate, data, pixels, background.size, None
    except (OSError, ValueError, RuntimeError, Image.DecompressionBombError, Image.DecompressionBombWarning) as error:
        return candidate, None, None, None, str(error)


def save_manifest(root, records):
    records.sort(key=lambda record: record["auction_id"])
    atomic_json(root / "database/image_manifest.json", records)
    credits = root / "database/image_credits.csv"
    temp = credits.with_suffix(".csv.tmp")
    with temp.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CREDIT_FIELDS)
        writer.writeheader()
        for record in records:
            writer.writerow({key: record[key] for key in CREDIT_FIELDS})
    replace_file(temp, credits)


def seed_records(categories, manifest):
    by_id = {image["auction_id"]: image for image in manifest}
    records = []
    for category in categories:
        for number in range(1, 101):
            auction_id = (category["id"] - 1) * 100 + number
            product = category["products"][(number - 1) // 10]
            image = by_id.get(auction_id)
            title = f'{product.capitalize()} — auction lot {(number - 1) % 10 + 1:02d}'
            description = (f'{product.capitalize()} offered as one individual lot in the {category["name"]} collection. '
                           'The local photograph is the reference for its appearance; dimensions, condition and provenance '
                           'must be confirmed before a future sale. This is demonstration inventory, not a verified offer.')
            if image and image["source_description"]:
                description += " Photo documentation: " + image["source_description"][:800]
            end = datetime(2026, 12, 1, 18, tzinfo=timezone.utc) + timedelta(minutes=auction_id * 15)
            records.append(dict(id=auction_id, title=title, description=description,
                                image_url=f'/images/products/{category["slug"]}-{number:03d}.jpg',
                                category_id=category["id"],
                                starting_price_cents=category["base_price_cents"] + number * 175,
                                starts_at="2026-09-12T00:00:00Z", ends_at=end.strftime("%Y-%m-%dT%H:%M:%SZ"),
                                created_at="2026-09-12T00:00:00Z", updated_at="2026-09-12T00:00:00Z"))
    return records


def verify(root, categories, records):
    if len(records) != 1000:
        raise ValueError(f"Manifest has {len(records)} images; exactly 1000 required")
    expected = {f'public/images/products/{category["slug"]}-{number:03d}.jpg'
                for category in categories for number in range(1, 101)}
    actual = {str(path.relative_to(root)).replace("\\", "/")
              for path in (root / "public/images/products").iterdir() if path.is_file()}
    if actual != expected or {record["local_path"] for record in records} != expected:
        raise ValueError("Expected exactly 1000 predictable local files with no extra files")
    for field in ["auction_id", "local_path", "original_url", "file_page_url", "source_sha1", "sha256", "pixel_sha256"]:
        if len({record[field] for record in records}) != 1000:
            raise ValueError(f"Duplicate {field}")
    for category in categories:
        if sum(record["category"] == category["name"] for record in records) != 100:
            raise ValueError(f'Wrong image count for {category["name"]}')
    for record in records:
        path = root / record["local_path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"Checksum mismatch: {path}")
        with Image.open(path) as image:
            image.verify()
        with Image.open(path) as image:
            if image.format not in {"JPEG", "PNG"} or image.width > 480 or image.height > 320:
                raise ValueError(f"Invalid image or dimensions: {path}")
        if not all(record.get(key) for key in ["author", "license", "license_url", "file_page_url"]):
            raise ValueError("Missing attribution")
    seeds = read_json(root / "database/seed_auctions.json")
    mapping = {record["auction_id"]: "/" + record["local_path"].removeprefix("public/") for record in records}
    if len(seeds) != 1000 or any(mapping.get(seed["id"]) != seed["image_url"] for seed in seeds):
        raise ValueError("Auction-to-image mapping mismatch")
    print("VERIFIED: 10 categories; 1000 auctions; 1000 distinct local photos; 100 per category", flush=True)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Offline strict validation; exits nonzero on missing assets")
    parser.add_argument("--prepare-seed", action="store_true", help="Write deterministic seed data, without claiming images exist")
    args = parser.parse_args()
    categories = read_json(ROOT / "database/seed_categories.json")
    path = ROOT / "database/image_manifest.json"
    records = read_json(path) if path.exists() else []
    if args.prepare_seed:
        atomic_json(ROOT / "database/seed_auctions.json", seed_records(categories, records))
        print(f"Seed written; {len(records)}/1000 images recorded. No download claimed.")
        return
    if args.verify_only:
        verify(ROOT, categories, records)
        return
    # Fail clearly before changing any assets if the API is inaccessible.
    json.loads(fetch(API + "?action=query&format=json&meta=siteinfo"))
    (ROOT / "public/images/products").mkdir(parents=True, exist_ok=True)
    save_manifest(ROOT, records)
    seen = {key: {record[key] for record in records} for key in ["original_url", "source_sha1", "sha256", "pixel_sha256"]}
    for record in records:
        file = ROOT / record["local_path"]
        if not file.exists() or hashlib.sha256(file.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"Existing import damaged: {file}; restore it before resuming")
    with ThreadPoolExecutor(max_workers=3) as workers:
        for category in categories:
            for group, term in enumerate(category["products"]):
                numbers = list(range(group * 10 + 1, group * 10 + 11))
                imported = {record["auction_id"] for record in records}
                missing = [number for number in numbers if (category["id"] - 1) * 100 + number not in imported]
                if not missing:
                    continue
                batches = category_batches(category["commons_categories"][group])
                for pool in batches:
                    if not missing:
                        break
                    pool = [item for item in pool if all(item[key] not in seen[key] for key in ["original_url", "source_sha1"])]
                    while pool and missing:
                        batch, pool = pool[:len(missing)], pool[len(missing):]
                        for candidate, data, pixels, size, error in workers.map(photograph, batch):
                            if error:
                                print(f'SKIP {candidate["source_title"]}: {error}', flush=True)
                                continue
                            digest = hashlib.sha256(data).hexdigest()
                            if digest in seen["sha256"] or pixels in seen["pixel_sha256"] or any(candidate[key] in seen[key] for key in ["original_url", "source_sha1"]):
                                continue
                            number = missing.pop(0)
                            record = dict(candidate, auction_id=(category["id"] - 1) * 100 + number,
                                          category=category["name"], product_type=term,
                                          local_path=f'public/images/products/{category["slug"]}-{number:03d}.jpg',
                                          sha256=digest, pixel_sha256=pixels, width=size[0], height=size[1],
                                          modifications="Resized to fit 480x320; orientation applied; converted to JPEG; metadata removed")
                            (ROOT / record["local_path"]).write_bytes(data)
                            records.append(record)
                            for key in seen:
                                seen[key].add(record[key])
                            save_manifest(ROOT, records)
                            print(f'[{len(records)}/1000] {record["local_path"]}: {record["source_title"]}', flush=True)
                    time.sleep(0.15)
                if missing:
                    for pool in search_batches(term):
                        if not missing:
                            break
                        pool = [item for item in pool if all(item[key] not in seen[key] for key in ["original_url", "source_sha1"])]
                        while pool and missing:
                            batch, pool = pool[:len(missing)], pool[len(missing):]
                            for candidate, data, pixels, size, error in workers.map(photograph, batch):
                                if error:
                                    print(f'SKIP {candidate["source_title"]}: {error}', flush=True)
                                    continue
                                digest = hashlib.sha256(data).hexdigest()
                                if digest in seen["sha256"] or pixels in seen["pixel_sha256"] or any(candidate[key] in seen[key] for key in ["original_url", "source_sha1"]):
                                    continue
                                number = missing.pop(0)
                                record = dict(candidate, auction_id=(category["id"] - 1) * 100 + number,
                                              category=category["name"], product_type=term,
                                              local_path=f'public/images/products/{category["slug"]}-{number:03d}.jpg',
                                              sha256=digest, pixel_sha256=pixels, width=size[0], height=size[1],
                                              modifications="Resized to fit 480x320; orientation applied; converted to JPEG; metadata removed")
                                (ROOT / record["local_path"]).write_bytes(data)
                                records.append(record)
                                for key in seen:
                                    seen[key].add(record[key])
                                save_manifest(ROOT, records)
                                print(f'[{len(records)}/1000] {record["local_path"]}: {record["source_title"]}', flush=True)
                        time.sleep(0.15)
                if missing:
                    raise RuntimeError(f"Not enough usable photographs for {category['name']}/{term}: {len(missing)} missing. Import saved; refine this product's search term and rerun.")
    atomic_json(ROOT / "database/seed_auctions.json", seed_records(categories, records))
    verify(ROOT, categories, records)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        raise SystemExit(f"IMAGE IMPORT INCOMPLETE: {error}")



