#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fills in app/translations/*.ts from the tables below, after lupdate (run by the build) has
added new strings. Strings missing here stay unfinished and show in English.

    tools/translations.py
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Plural forms only; everything else is English in the source already.
EN = {
    "%n light(s)": ["%n light", "%n lights"],
    "%n spot(s)": ["%n spot", "%n spots"],
    "%n min ago": ["%n min ago", "%n min ago"],
    "%n h ago": ["%n h ago", "%n h ago"],
    "%n day(s) ago": ["%n day ago", "%n days ago"],
}

NL = {
    "Connecting": "Verbinden",
    "No bridge": "Geen bridge",
    "%n light(s)": ["%n lamp", "%n lampen"],
    "The bridge did not respond": "De bridge reageert niet",
    "The bridge closed the connection": "De bridge heeft de verbinding verbroken",
    "Unexpected answer from the bridge": "Onverwacht antwoord van de bridge",
    "The bridge no longer accepts this app; pair it again": "De bridge accepteert deze app niet meer; koppel hem opnieuw",
    "The bridge's certificate changed; pair it again if you trust it":
        "Het certificaat van de bridge is veranderd; koppel hem opnieuw als je hem vertrouwt",
    "This bridge's software is too old for this app. Update it in the Hue app (Settings, Software update)":
        "De software van deze bridge is te oud voor deze app. Werk hem bij in de Hue-app (Instellingen, Software-update)",
    "This is not a Hue bridge": "Dit is geen Hue bridge",
    "The link button wasn't pressed in time": "De knop is niet op tijd ingedrukt",
    "Unreachable": "Onbereikbaar",
    "%n spot(s)": ["%n spot", "%n spots"],
    "The bridge can't reach this light. Is it switched on at the wall?":
        "De bridge kan deze lamp niet bereiken. Staat de schakelaar aan de muur aan?",
    "On": "Aan",
    "Brightness": "Helderheid",
    "White": "Wit",
    "Colour temperature": "Kleurtemperatuur",
    "Colour": "Kleur",
    "Spots": "Spots",
    "Press the round button on top of the bridge": "Druk op de ronde knop boven op de bridge",
    "Try again": "Opnieuw proberen",
    "Scenes": "Scènes",
    "Lights": "Lampen",
    "Settings": "Instellingen",
    "Sensors and switches": "Sensoren en schakelaars",
    "All lights off": "Alle lampen uit",
    "Connecting…": "Verbinden…",
    "Reconnecting…": "Opnieuw verbinden…",
    "Zones": "Zones",
    "Rooms": "Ruimtes",
    "No rooms yet": "Nog geen ruimtes",
    "Rooms and zones are set up in the Hue app": "Ruimtes en zones stel je in met de Hue-app",
    "Pull down for settings": "Trek omlaag voor instellingen",
    "just now": "zojuist",
    "%n min ago": ["%n min geleden", "%n min geleden"],
    "%n h ago": ["%n uur geleden", "%n uur geleden"],
    "yesterday": "gisteren",
    "%n day(s) ago": ["%n dag geleden", "%n dagen geleden"],
    "pressed": "ingedrukt",
    "held": "vastgehouden",
    "Switched off": "Uitgeschakeld",
    "Motion": "Beweging",
    "No motion": "Geen beweging",
    "%1 °C": "%1 °C",
    "%1 lux": "%1 lux",
    "Button %1 %2": "Knop %1 %2",
    "Battery low: %1 %": "Batterij bijna leeg: %1 %",
    "Battery %1 %": "Batterij %1 %",
    "No sensors or switches": "Geen sensoren of schakelaars",
    "Bridge": "Bridge",
    "Name": "Naam",
    "Address": "Adres",
    "Status": "Status",
    "Connected": "Verbonden",
    "Waiting to reconnect": "Wacht om opnieuw te verbinden",
    "Not paired": "Niet gekoppeld",
    "Certificate changed": "Certificaat veranderd",
    "Reconnect": "Opnieuw verbinden",
    "Forget bridge": "Bridge vergeten",
    "Forgetting bridge": "Bridge vergeten",
    "Forgetting only removes the bridge from this phone. To revoke the app's access, remove \"lampje#sailfishos\" in the Hue app.":
        "Vergeten verwijdert de bridge alleen van deze telefoon. Verwijder \"lampje#sailfishos\" in de Hue-app om de app de toegang te ontnemen.",
    "Search again": "Opnieuw zoeken",
    "Find your bridge": "Zoek je bridge",
    "Connect your phone to the same network as your Hue bridge.":
        "Verbind je telefoon met hetzelfde netwerk als je Hue bridge.",
    "Bridges found": "Gevonden bridges",
    "%1 · this bridge is too old": "%1 · deze bridge is te oud",
    "No bridges found": "Geen bridges gevonden",
    "Or enter its address": "Of vul het adres in",
    "IP address": "IP-adres",
}


def fill(path, table, language):
    tree = ET.parse(path)
    root = tree.getroot()
    root.set("language", language)
    missing = []
    for message in root.iter("message"):
        source = message.findtext("source")
        translation = message.find("translation")
        value = table.get(source)
        if value is None:
            if table is not EN:
                missing.append(source)
            continue
        for child in list(translation):
            translation.remove(child)
        translation.text = None
        if message.get("numerus") == "yes":
            for form in value:
                ET.SubElement(translation, "numerusform").text = form
        else:
            translation.text = value
        translation.attrib.pop("type", None)
    tree.write(path, encoding="utf-8", xml_declaration=True)
    with open(path, "a", encoding="utf-8") as f:
        f.write("\n")
    return missing


def main():
    folder = Path(__file__).resolve().parent.parent / "app" / "translations"
    fill(folder / "harbour-lampje-en.ts", EN, "en")
    missing = fill(folder / "harbour-lampje-nl.ts", NL, "nl")
    for source in missing:
        print(f"nl: not translated: {source!r}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
