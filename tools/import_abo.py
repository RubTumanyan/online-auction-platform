#!/usr/bin/env python3
"""Replace the demo catalog with a deterministic sample of 1000 Amazon Berkeley Objects products.

Amazon Berkeley Objects (c) by Amazon.com. The listings and small images are read from the
local ``abo-listings.tar`` and ``abo-images-small.tar`` archives under CC BY 4.0; attribution
is recorded in ``database/image_credits.csv`` and ``database/image_manifest.json``.

Selection model (mirrored by server ``Database.cpp::seed``):
  * exactly 10 categories in ``database/seed_categories.json`` (ids 1..10)
  * exactly 1000 auctions in ``database/seed_auctions.json`` (ids 1..1000, 100 per category)
  * exactly 1000 unique JPEGs in ``public/images/products/`` named ``{slug}-{nnn}.jpg``
  * ``database/image_manifest.json`` maps each auction to its local file, dimensions,
    checksum and attribution (verified by tests/DatabaseTests.cpp --verify-images)

Listings are consumed in shard order; English-only. Each product is scored against the
keyword lists of the ten topic buckets (product type weighted 3, item name weighted 2,
bullet points weighted 1). The highest-scoring bucket with capacity takes the listing;
ties and unrecognised product types fill the most sparingly filled bucket. This doubles as
the deterministic price source (category base plus a per-item SHA-256 offset) and keeps
lot 1 within the bounds used by tests/AuthBidHttpTests.cpp.

Usage:
    python tools/import_abo.py [options]

Options:
    --listings-tar PATH  Path to abo-listings.tar (default: <Downloads>/abo-listings.tar)
    --images-tar PATH    Path to abo-images-small.tar (default: <Downloads>/abo-images-small.tar)
    --work DIR           Scratch directory for extracted images (default: a temp subdir)
"""

import argparse
import csv
import datetime
import gzip
import hashlib
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
IMAGE_MEMBER_PREFIX = "images/small/"
ABO_SOURCE_URL = "https://amazon-berkeley-objects.s3.us-east-1.amazonaws.com/index.html"
CDN_TEMPLATE = "https://m.media-amazon.com/image/I/{image_id}.jpg"
CC_BY_4_0 = "https://creativecommons.org/licenses/by/4.0/"

# Category order defines the seed ids in database/seed_categories.json. Materials &
# Consumables is deliberately id 1: its prices stay under the bid-test bounds that
# tests/AuthBidHttpTests.cpp hard-codes for lot 1.
#
# Prices: starting_price_cents = base - 400 + (SHA-256(item_id) % 1301), so each bucket
# stays within [-400, +900] of its base and lot 1 (base 2200) never exceeds 3155.
BUCKETS = [
    {
        "slug": "materials",
        "name": "Materials & Consumables",
        "base": 2200,
        "keywords": [
            "hardware", "mechanical", "component", "fastener", "screw", "bolts", "bolt",
            "nut", "washer", "hinge", "latch", "bracket", "rail", "slide", "drawer slide",
            "filament", "adhesive", "tape", "glue", "sealant", "caulk", "lubricant", "paint",
            "coating", "finish", "spring", "magnets", "magnet", "rope", "twine", "strap",
            "gasket", "ring", "key", "lock", "keycap", "keychain", "nail", "tack", "staple",
            "pin", "clamp", "vise", "tool", "wrench", "pliers", "hammer", "drill", "saw",
            "wax", "oil", "detergent", "cleaner", "cleaning", "soap", "sponge", "filter",
            "battery", "cable", "wire", "connector", "terminal", "socket", "plug", "rubber",
            "silicone", "supplies", "consumable", "sticker", "label", "bag", "pouch",
            "packaging", "foam", "cushion", "utensil", "material", "parts", "replacement",
        ],
    },
    {
        "slug": "home-furniture",
        "name": "Home & Furniture",
        "base": 12000,
        "keywords": [
            "furniture", "table", "chair", "sofa", "couch", "bench", "stool", "ottoman",
            "bed frame", "bed", "headboard", "mattress", "frame", "desk", "wardrobe",
            "armoire", "dresser", "cabinet", "cupboard", "shelf", "shelving", "bookcase",
            "book shelf", "vanity", "nightstand", "sideboard", "buffet", "credenza",
            "console", "bureau", "chest", "counter", "bar", "barstool", "recliner",
            "seating", "futon", "rocking", "footrest", "foldable", "dining set", "couch",
            "modular", "storage bed", "trundle", "umbrella stand", "coat rack", "room",
        ],
    },
    {
        "slug": "kitchen-dining",
        "name": "Kitchen & Dining",
        "base": 4500,
        "keywords": [
            "kitchen", "cookware", "pan", "pot", "kettle", "toaster", "blender", "mixer",
            "juicer", "coffee", "espresso", "grinder", "food", "food processor", "knife",
            "cutlery", "dish", "plate", "bowl", "cup", "mug", "glass", "tumbler", "tray",
            "chopping", "cutting board", "grater", "peeler", "colander", "strainer",
            "measuring", "spoon", "fork", "knife", "ladle", "tongs", "bakeware", "bread",
            "oven", "microwave", "appliance", "beverage", "drinking", "dinnerware",
            "tableware", "napkin", "serveware", "espresso machine", "wine", "beer", "cork",
            "bottle opener", "water bottle",
        ],
    },
    {
        "slug": "storage-organization",
        "name": "Storage & Organization",
        "base": 3000,
        "keywords": [
            "storage", "organizer", "organizing", "bin", "basket", "container", "box",
            "garment", "closet", "hanger", "rack", "holder", "divider", "compartment",
            "caddy", "cart", "trolley", "hamper", "crate", "tote", "pocket", "sleeve",
            "pegs", "peg", "drawer", "drawers", "shoe rack", "umbrella", "wall rack",
            "mounted", "hanging organizer", "under bed", "vacuum storage", "wardrobe box",
        ],
    },
    {
        "slug": "audio-electronics",
        "name": "Audio & Electronics",
        "base": 8500,
        "keywords": [
            "audio", "speaker", "earphone", "earbud", "headphone", "microphone", "amp",
            "amplifier", "receiver", "turntable", "record player", "tv", "television",
            "screen", "display", "projector", "camera", "camcorder", "lens", "drone",
            "gps", "wearable", "smartwatch", "fitness tracker", "e-reader", "video", "dvd",
            "blu-ray", "soundbar", "subwoofer", "music", "karaoke", "antenna", "tuner",
            "radio", "boombox", "electronic", "electric", "charger", "power", "adapter",
            "power strip", "surge", "inverter", "solar", "surveillance", "security camera",
            "smart home", "robot vacuum", "purifier", "humidifier", "dehumidifier", "fan",
            "heater", "air conditioner", "vacuum", "steam", "iron", "hair dryer", "shaver",
            "trimmer", "razor", "kettle", "monitor", "phone", "cellular", "computer",
            "sound", "music",
        ],
    },
    {
        "slug": "computers-office",
        "name": "Computers & Office",
        "base": 9000,
        "keywords": [
            "computer", "laptop", "notebook", "keyboard", "mouse", "printer", "scanner",
            "tablet", "pc", "cpu", "graphics", "motherboard", "ram", "ssd", "hdd",
            "storage drive", "usb", "flash drive", "sd card", "memory card", "docking",
            "hub", "webcam", "router", "modem", "network", "wifi", "ethernet", "office",
            "stationery", "paper", "pen", "pencil", "marker", "highlighter", "eraser",
            "clipboard", "binder", "folder", "file", "envelope", "notebook", "journal",
            "calculator", "cartridge", "toner", "headset", "desk lamp", "earphones",
        ],
    },
    {
        "slug": "bedding-textiles",
        "name": "Bedding & Textiles",
        "base": 2500,
        "keywords": [
            "bedding", "sheet", "pillowcase", "pillow", "comforter", "duvet", "quilt",
            "blanket", "throw", "coverlet", "towel", "curtain", "drape", "valance", "sheer",
            "textile", "fabric", "cloth", "upholstery", "linen", "flannel", "fleece",
            "cotton", "silk", "wool", "felt", "lace", "apparel", "clothing", "shirt",
            "footwear", "shoe", "dress", "jacket", "coat", "sweater", "hat", "scarf",
            "glove", "sock", "underwear", "outerwear", "garment", "uniform", "costume",
            "apron", "sleeping bag liner", "shower curtain", "bath", "spa", "towel",
            "washcloth",
        ],
    },
    {
        "slug": "lighting",
        "name": "Lighting",
        "base": 4000,
        "keywords": [
            "light", "lamp", "bulb", "led", "fixture", "chandelier", "sconce", "pendant",
            "luminaire", "track lighting", "flashlight", "lantern", "string light",
            "night light", "ceiling light", "floor lamp", "candelabra", "torch",
            "headlamp", "smart bulb", "illuminat", "glow", "backlit",
        ],
    },
    {
        "slug": "decor-art",
        "name": "Décor & Art",
        "base": 3500,
        "keywords": [
            "decor", "decorative", "art", "picture", "frame", "poster", "print", "painting",
            "canvas", "sculpture", "figurine", "statuette", "vase", "ceramic", "pottery",
            "clock", "mirror", "wall", "tapestry", "macrame", "wreath", "garland", "candle",
            "holder", "rug", "mat", "doormat", "runner", "ornament", "statue", "bust",
            "trophy", "photo", "coaster", "table runner", "centerpiece", "seasonal",
            "holiday", "christmas", "frame wall", "artwork",
        ],
    },
    {
        "slug": "outdoor-garden",
        "name": "Outdoor & Garden",
        "base": 5500,
        "keywords": [
            "outdoor", "garden", "patio", "lawn", "yard", "plant", "planter", "flower",
            "seed", "soil", "fertilizer", "mulch", "hose", "sprinkler", "nozzle", "watering",
            "shovel", "spade", "rake", "hoe", "pruner", "trimmer", "mower", "edger",
            "chainsaw", "grill", "barbecue", "bbq", "smoker", "grilling", "camping", "tent",
            "sleeping bag", "picnic", "cooler", "thermos", "flask", "canteen", "hiking",
            "bicycle", "bike", "cycling", "scooter", "skateboard", "trampoline", "pool",
            "spa", "jacuzzi", "sauna", "shed", "gazebo", "greenhouse", "fence", "gate",
            "pest", "insect", "bird", "wildlife",
        ],
    },
]

_HTML_TAG_RE = re.compile(r"<[^>]+>")
_ENTITY_RE = re.compile(r"&(?:nbsp|amp|lt|gt|quot|#39|apos);", re.IGNORECASE)
_ENTITY_MAP = {
    "&nbsp;": " ", "&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": '"',
    "&#39;": "'", "&apos;": "'",
}

# Product types that are perishable or consumable and unsuitable as auction lots.
BLOCKED_PRODUCT_TYPES = {
    "grocery", "grocery_and_gourmet", "food", "food_and_beverage", "beverage",
    "beverages", "snack", "snacks", "supplement", "supplements", "vitamin",
    "vitamins", "drink", "drinks", "candy", "confectionery", "confectionary",
    "dairy", "produce", "pet_food", "pet_supplies", "pet_products", "bakery",
    "frozen_food", "meat", "seafood", "frozen", "health", "beauty", "drugstore",
    "toiletries", "cosmetic", "skincare", "personal_care",
}


def tokens(text):
    """Lowercase alphanumeric tokens, matching ABO product-type and name text."""
    return re.findall(r"[0-9a-z]+", text.lower())


def keyword_match(token, keyword):
    """True when ``token`` equals ``keyword`` or is a natural variant of it."""
    if token == keyword:
        return True
    if len(keyword) >= 5 and token.startswith(keyword):
        return True
    if token.endswith("ies") and token[:-3] + "y" == keyword:
        return True
    if token.endswith("es") and token[:-2] == keyword:
        return True
    if token.endswith("s") and token[:-1] == keyword:
        return True
    return False


def clean(text):
    """Strip HTML, unescape common entities and collapse whitespace."""
    if not text:
        return ""
    for key, value in _ENTITY_MAP.items():
        text = text.replace(key, value)
    text = _HTML_TAG_RE.sub(" ", text)
    return re.sub(r"[ \t\r\f\v]+", " ", text).strip()


def localized(values, prefix="en"):
    """Return the first non-empty value whose language tag starts with ``prefix``."""
    entries = values if isinstance(values, list) else [values]
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        tag = entry.get("language_tag") or ""
        value = entry.get("value")
        if isinstance(value, str) and value.strip() and tag.lower().startswith(prefix.lower()):
            return value.strip()
    return ""


def starting_price_cents(base, item_id):
    digest = hashlib.sha256(item_id.encode("utf-8")).digest()
    variance = int.from_bytes(digest[:4], "big") % 1301  # -400 .. +900
    cents = int(round((base - 400 + variance) / 5.0) * 5)
    return max(cents, 500)


def ends_at(auction_id):
    base = datetime.datetime(2026, 10, 31, 18, 0, tzinfo=datetime.timezone.utc)
    stamp = base + datetime.timedelta(seconds=auction_id * 600)
    return stamp.strftime("%Y-%m-%dT%H:%M:%SZ")


def image_csv_records(images_tar):
    """Parse images/metadata/images.csv.gz from the archive into {image_id: (path, ext)}."""
    gz = subprocess.run(
        ["tar", "-xOf", str(images_tar), "images/metadata/images.csv.gz"],
        check=True, capture_output=True, text=False).stdout
    reader = csv.DictReader(io.StringIO(gzip.decompress(gz).decode("utf-8")))
    records = {}
    for row in reader:
        path = row["path"].strip()
        ext = path.rsplit(".", 1)[-1].lower()
        records[row["image_id"].strip()] = (path, ext)
    return records


def pick_bucket(pt, name, bullets):
    """Score a listing against each bucket by weighted token matches.

    Product_type tokens weigh 3, item name tokens 2 and bullet tokens 1. Returns
    ``(bucket_index, score)`` for the strongest bucket, or ``(None, 0)`` when no
    keyword matched. Order-independent: phase 2 fills each bucket with its best
    candidates, and score-0 listings rest in the most sparingly filled bucket.
    """
    pt_tokens, name_tokens, bullet_tokens = tokens(pt), tokens(name), tokens(bullets)
    best, best_score = None, 0
    for index, bucket in enumerate(BUCKETS):
        score = 0
        for token in pt_tokens:
            if any(keyword_match(token, key) for key in bucket["keywords"]):
                score += 3
        for token in name_tokens:
            if any(keyword_match(token, key) for key in bucket["keywords"]):
                score += 2
        for token in bullet_tokens:
            if any(keyword_match(token, key) for key in bucket["keywords"]):
                score += 1
        if score > best_score:
            best, best_score = index, score
    return best, best_score


def select_listings(listings_tar, image_index, member_jpg):
    """Score every English listing, then keep the 100 best matches per bucket."""
    candidates = []
    counter = 0
    seen_items = set()
    shards = sorted(
        name for name in member_names(listings_tar)
        if name.startswith("listings/metadata/listings_") and name.endswith(".json.gz"))

    with tarfile.open(listings_tar, "r") as archive:
        for shard in shards:
            handle = archive.extractfile(archive.getmember(shard))
            stream = gzip.open(handle, "rt", encoding="utf-8", errors="replace")
            for line in stream:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, dict):
                    continue
                item_id = record.get("item_id", "")
                if not item_id or item_id in seen_items:
                    continue
                name = localized(record.get("item_name"))
                if not name:
                    continue
                product_type = record.get("product_type")
                if isinstance(product_type, list):
                    product_type = product_type[0].get("value", "") if isinstance(product_type[0], dict) else str(product_type[0])
                elif not isinstance(product_type, str):
                    product_type = ""
                if not product_type or product_type.casefold() in BLOCKED_PRODUCT_TYPES:
                    continue
                bullets = clean(localized(record.get("bullet_point")))
                description = clean(
                    localized(record.get("product_description")) or bullets or name)
                image_id = record.get("main_image_id", "")
                image = image_index.get(image_id)
                if not image or image[1] != "jpg":
                    continue
                member = f"{IMAGE_MEMBER_PREFIX}{image[0]}"
                if member not in member_jpg:
                    continue
                seen_items.add(item_id)
                bucket, score = pick_bucket(product_type, name, bullets)
                candidates.append({
                    "item_id": item_id,
                    "title": clean(name),
                    "description": description,
                    "product_type": product_type,
                    "image_id": image_id,
                    "member": member,
                    "order": counter,
                    "bucket": bucket,
                    "score": score,
                })
                counter += 1

    # Phase 2: per bucket, prefer the strongest matches (scan order breaks ties).
    wanted = [[] for _ in BUCKETS]
    used_images = set()
    for index, bucket in enumerate(BUCKETS):
        ranked = sorted(
            (candidate for candidate in candidates if candidate["bucket"] == index),
            key=lambda candidate: (-candidate["score"], candidate["order"]))
        for candidate in ranked:
            if len(wanted[index]) == 100:
                break
            if candidate["image_id"] in used_images:
                continue
            used_images.add(candidate["image_id"])
            wanted[index].append(candidate)
        if len(wanted[index]) == 100:
            continue
        # Top up from every remaining candidate in scan order.
        for candidate in candidates:
            if len(wanted[index]) == 100:
                break
            if candidate["image_id"] in used_images or candidate in wanted[index]:
                continue
            used_images.add(candidate["image_id"])
            wanted[index].append(candidate)
    return wanted


def member_names(tar_path):
    with tarfile.open(tar_path, "r") as archive:
        return list(archive.getnames())


def main():
    parser = argparse.ArgumentParser(description="Import 1000 Amazon Berkeley Objects products into the demo catalog.")
    downloads = pathlib.Path(os.environ.get("USERPROFILE", "")) / "Downloads"
    parser.add_argument("--listings-tar", default=str(downloads / "abo-listings.tar"))
    parser.add_argument("--images-tar", default=str(downloads / "abo-images-small.tar"))
    args = parser.parse_args()

    listings_tar, images_tar = pathlib.Path(args.listings_tar), pathlib.Path(args.images_tar)
    if not listings_tar.is_file() or not images_tar.is_file():
        sys.exit(f"Missing archive: {listings_tar} or {images_tar}")

    print(f"[1/6] Indexing images.csv from {images_tar.name} ...")
    image_index = image_csv_records(images_tar)

    print("[2/6] Reading image archive member list ...")
    with tarfile.open(images_tar, "r") as archive:
        member_jpg = {name for name in archive.getnames()
                      if name.startswith(IMAGE_MEMBER_PREFIX) and name.lower().endswith(".jpg")}

    print(f"[3/6] Selecting 1000 English listings from {listings_tar.name} ...")
    selected = select_listings(listings_tar, image_index, member_jpg)

    slots = [(index, len(items)) for index, items in enumerate(selected)]
    too_small = [index for index, size in slots if size != 100]
    if too_small:
        for index in too_small:
            print(f"  !! bucket {BUCKETS[index]['slug']} only filled {len(selected[index])}/100")
        sys.exit("Not enough valid listings per bucket; selection incomplete.")
    print("  filled 10 buckets x 100 lots")

    # Deterministic final order: ids 1..1000 grouped by category.
    lots = []
    by_product_type = {}
    for bucket_index, bucket in enumerate(BUCKETS):
        bucket["_names"] = []
        by_product_type[bucket_index] = []
        for local, item in enumerate(selected[bucket_index]):
            auction_id = bucket_index * 100 + local + 1
            image_url = f"/images/products/{bucket['slug']}-{local + 1:03d}.jpg"
            lots.append({
                "id": auction_id,
                "title": item["title"],
                "description": item["description"][:1600],
                "image_url": image_url,
                "category_id": bucket_index + 1,
                "starting_price_cents": starting_price_cents(bucket["base"], item["item_id"]),
                "starts_at": "2026-09-13T00:00:00Z",
                "ends_at": ends_at(auction_id),
                "created_at": "2026-09-13T00:00:00Z",
                "updated_at": "2026-09-13T00:00:00Z",
                "item_id": item["item_id"],
                "image_id": item["image_id"],
                "member": item["member"],
            })
            if len(bucket["_names"]) < 8:
                bucket["_names"].append(item["title"])
            if len(by_product_type[bucket_index]) < 6:
                by_product_type[bucket_index].append(item["product_type"])

    print("[4/6] Extracting 1000 product images (single pass over the archive) ...")
    work = pathlib.Path(tempfile.mkdtemp(prefix="abo-images-"))
    wanted = {lot["member"] for lot in lots}
    extracted = {}
    with tarfile.open(images_tar, "r") as archive:
        member = archive.next()
        while member is not None:
            if member.name in wanted:
                data = archive.extractfile(member).read()
                if data[:2] != b"\xff\xd8":
                    sys.exit(f"Not a JPEG image: {member.name}")
                extracted[member.name] = data
                if len(extracted) == len(wanted):
                    break
            member = archive.next()
    if len(extracted) != len(wanted):
        sys.exit(f"Extracted {len(extracted)} images, expected {len(wanted)}")

    print("[5/6] Building manifest, seeds and credits ...")
    manifest, credits = [], []
    categories, auctions = [], []
    products_dir = ROOT / "public" / "images" / "products"
    for stale in products_dir.glob("*.jpg"):
        stale.unlink()
    products_dir.mkdir(parents=True, exist_ok=True)
    for bucket_index, bucket in enumerate(BUCKETS):
        categories.append({
            "id": bucket_index + 1,
            "name": bucket["name"],
            "slug": bucket["slug"],
            "products": bucket["_names"],
            "base_price_cents": bucket["base"],
            "commons_categories": by_product_type[bucket_index],
        })

    for lot in lots:
        data = extracted[lot["member"]]
        digest = hashlib.sha256(data).hexdigest()
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.width, image.height
        if width <= 0 or height <= 0 or width > 480 or height > 320:
            sys.exit(f"Image exceeds manifest bounds: {lot['image_url']} {width}x{height}")
        file_name = lot["image_url"].rsplit("/", 1)[1]
        (products_dir / file_name).write_bytes(data)
        lot["sha256"] = digest
        lot["width"], lot["height"] = width, height
        lot["local_path"] = f"public/images/products/{file_name}"
        # Attribution per the bundled LICENSE-CC-BY-4.0.txt.
        lot["author"] = "Amazon.com"
        lot["license"] = "CC BY 4.0"
        lot["source_title"] = lot["title"]
        lot["source_sha1"] = hashlib.sha1(f"{lot['item_id']}:{lot['image_id']}".encode()).hexdigest()
        manifest.append({
            "auction_id": lot["id"],
            "original_url": CDN_TEMPLATE.format(image_id=lot["image_id"]),
            "file_page_url": CDN_TEMPLATE.format(image_id=lot["image_id"]),
            "thumbnail_url": CDN_TEMPLATE.format(image_id=lot["image_id"]),
            "source_sha1": lot["source_sha1"],
            "sha256": digest,
            "width": width,
            "height": height,
            "author": "Amazon.com",
            "author_url": ABO_SOURCE_URL,
            "license": "CC BY 4.0",
            "license_url": CC_BY_4_0,
            "source_title": lot["title"],
            "source_description": lot["description"][:300],
            "local_path": lot["local_path"],
            "pixel_sha256": digest,
        })
        credits.append({
            "local_path": lot["local_path"],
            "file_page_url": manifest[-1]["file_page_url"],
            "author": "Amazon.com",
            "author_url": ABO_SOURCE_URL,
            "license": "CC BY 4.0",
            "license_url": CC_BY_4_0,
            "category": BUCKETS[lot["category_id"] - 1]["name"],
            "modifications": "downscaled by dataset; no further edits",
        })
        auctions.append({key: lot[key] for key in [
            "id", "title", "description", "image_url", "category_id",
            "starting_price_cents", "starts_at", "ends_at", "created_at", "updated_at"]})

    manifest.sort(key=lambda record: record["auction_id"])
    credits.sort(key=lambda record: record["local_path"])

    for path, value in (
        (ROOT / "database" / "seed_categories.json", categories),
        (ROOT / "database" / "seed_auctions.json", auctions),
        (ROOT / "database" / "image_manifest.json", manifest),
    ):
        path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    credit_lines = ["local_path,file_page_url,author,author_url,license,license_url,category,modifications"]
    for row in credits:
        credit_lines.append(",".join(row.values()))
    (ROOT / "database" / "image_credits.csv").write_text("\n".join(credit_lines) + "\n", encoding="utf-8")

    print("[6/6] Verification ...")
    assert len(auctions) == 1000 and len(categories) == 10 and len(manifest) == 1000
    assert len({lot["image_url"] for lot in auctions}) == 1000
    assert all(0 < lot["starting_price_cents"] < 20000 for lot in auctions)
    assert all(lot["description"].strip() and lot["title"].strip() for lot in auctions)
    assert all(lot["image_url"].startswith("/images/products/") for lot in auctions)
    prices = {bucket["slug"]: [] for bucket in BUCKETS}
    for lot in auctions:
        prices[BUCKETS[lot["category_id"] - 1]["slug"]].append(lot["starting_price_cents"])
    print("  categories:", ", ".join(f"{bucket['name']} ({bucket['base']})" for bucket in BUCKETS))
    print("  prices:", {slug: (min(v), max(v)) for slug, v in prices.items()})
    print(f"  images: {len(list(products_dir.glob('*.jpg')))} files, sha256 unique "
          f"{len({lot['sha256'] for lot in lots}) == 1000}")
    print("Done. Rebuild the debug target so the server copies the regenerated catalog.")


if __name__ == "__main__":
    main()