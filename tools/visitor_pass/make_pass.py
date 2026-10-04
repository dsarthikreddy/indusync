"""
CLASSYNC 4.0 - VISITOR QR PASS MAKER (Stage 1: offline, one-time passes)

What it does
  Makes a QR code image for a visitor. The QR holds the visitor's name, a random
  pass ID and a secret signature. The ESP32-S3 checks the signature (so nobody can
  make fake passes) and remembers every pass it has used (so each pass works ONCE).

Install once (Command Prompt):
    pip install qrcode[pil]

Run (Command Prompt, inside this folder):
    python make_pass.py
"""
import csv
import datetime
import hashlib
import hmac
import os
import secrets
import string
import sys

import qrcode

# Must be EXACTLY the same as QR_SECRET in the ESP32 code (classync_stage1_v4.ino)
SECRET_KEY = os.environ.get("CLASSYNC_QR_SECRET", "REPLACE_WITH_YOUR_QR_SECRET")   # same text as QR_SECRET in secrets.h

PASS_FOLDER = "passes"
LOG_FILE = "passes_log.csv"


def clean_name(raw):
    """Keep only letters and numbers, in capitals, max 12 characters."""
    name = "".join(ch for ch in raw.upper() if ch.isalnum() and ch.isascii())
    return name[:12]


def make_pass_id():
    """Random 6-character pass ID, e.g. 7K2QXA."""
    alphabet = string.ascii_uppercase + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(6))


def sign(message):
    """Secret signature: first 12 characters of HMAC-SHA256."""
    digest = hmac.new(SECRET_KEY.encode(), message.encode(), hashlib.sha256).hexdigest()
    return digest.upper()[:12]


def make_pass(visitor):
    name = clean_name(visitor)
    if not name:
        raise ValueError("The name must contain letters or numbers.")
    pass_id = make_pass_id()
    message = "CSY:" + name + ":" + pass_id
    qr_text = message + ":" + sign(message)

    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,
                       box_size=12, border=4)
    qr.add_data(qr_text)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white")

    os.makedirs(PASS_FOLDER, exist_ok=True)
    filename = os.path.join(PASS_FOLDER, "pass_" + name + "_" + pass_id + ".png")
    img.save(filename)

    new_log = not os.path.exists(LOG_FILE)
    with open(LOG_FILE, "a", newline="") as f:
        writer = csv.writer(f)
        if new_log:
            writer.writerow(["created", "visitor", "pass_id", "file"])
        writer.writerow([datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
                         name, pass_id, filename])
    return name, pass_id, qr_text, filename


def main():
    print("=== CLASSYNC 4.0 visitor pass maker ===")
    visitor = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else input("Visitor name: ")
    try:
        name, pass_id, qr_text, filename = make_pass(visitor)
    except ValueError as e:
        print("ERROR:", e)
        return

    print("")
    print("Pass created!")
    print("  Visitor : " + name)
    print("  Pass ID : " + pass_id)
    print("  Image   : " + os.path.abspath(filename))
    print("  QR text : " + qr_text)
    print("")
    print("Show this image to the ESP32 camera. It works ONE time only.")

    if hasattr(os, "startfile"):      # Windows: open the image automatically
        os.startfile(os.path.abspath(filename))


if __name__ == "__main__":
    main()
