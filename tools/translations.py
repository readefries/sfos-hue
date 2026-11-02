#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fills in app/translations/*.ts and desktop/translations/*.ts from the tables below, after lupdate (run by the build) has
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
    "%1 + %n min": ["%1 + %n min", "%1 + %n min"],
    "%1 − %n min": ["%1 − %n min", "%1 − %n min"],
    "%n s": ["%n s", "%n s"],
    "%n min": ["%n min", "%n min"],
    "%n h": ["%n h", "%n h"],
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
    "New scene from current lights": "Nieuwe scène van de lampen nu",
    "Tap to switch to a scene; press and hold to change it": "Tik om een scène te kiezen; houd vast om hem aan te passen",
    "Delete scene": "Scène verwijderen",
    "Deleting scene": "Scène verwijderen",
    "New scene": "Nieuwe scène",
    "Save": "Opslaan",
    "Use the lights as they are now": "De lampen nemen zoals ze nu zijn",
    "Replaces what the scene does with the current state of the lights in %1":
        "Vervangt wat de scène doet door hoe de lampen in %1 nu staan",
    "The scene remembers the lights in %1 as they are now: which are on, how bright, and their colour.":
        "De scène onthoudt hoe de lampen in %1 nu staan: welke aan zijn, hoe fel en in welke kleur.",
    "Switch %1 on or off": "%1 aan of uit",
    "%1 in %2": "%1 in %2",
    "Nothing": "Niets",
    "Left cover button": "Linkerknop op de cover",
    "Right cover button": "Rechterknop op de cover",
    "Switch on or off": "Aan of uit",
    "Cover": "Cover",
    "The buttons on the app's cover, while it runs in the background":
        "De knoppen op de cover van de app, als die op de achtergrond draait",
    "Left button": "Linkerknop",
    "Right button": "Rechterknop",
    "New zone": "Nieuwe zone",
    "Edit zone": "Zone bewerken",
    "Delete zone": "Zone verwijderen",
    "Deleting zone": "Zone verwijderen",
    "A zone switches lights from different rooms together, like a room.":
        "Een zone schakelt lampen uit verschillende ruimtes samen, net als een ruimte.",
    "Kind": "Soort",
    "Living room": "Woonkamer",
    "Kitchen": "Keuken",
    "Dining": "Eetkamer",
    "Bedroom": "Slaapkamer",
    "Kids' bedroom": "Kinderkamer",
    "Bathroom": "Badkamer",
    "Office": "Kantoor",
    "Hallway": "Gang",
    "Garden": "Tuin",
    "Home": "Thuis",
    "Other": "Overig",
    "Guest room": "Logeerkamer",
    "Edit room": "Ruimte bewerken",
    "Rename": "Naam wijzigen",
    "Rename light": "Lamp hernoemen",
    "New room": "Nieuwe ruimte",
    "Not in a room": "Niet in een ruimte",
    "Room": "Ruimte",
    "No room": "Geen ruimte",
    "Delete room": "Ruimte verwijderen",
    "Deleting room": "Ruimte verwijderen",
    "Zigbee (MQTT)": "Zigbee (MQTT)",
    "Lights and sensors from zigbee2mqtt, through an MQTT broker":
        "Lampen en sensoren van zigbee2mqtt, via een MQTT-broker",
    "Broker": "Broker",
    "Refused": "Geweigerd",
    "Change": "Wijzigen",
    "Set up": "Instellen",
    "Remove": "Verwijderen",
    "Removing broker": "Broker verwijderen",
    "MQTT broker": "MQTT-broker",
    "Address, e.g. 192.168.1.10": "Adres, bijv. 192.168.1.10",
    "Port": "Poort",
    "User name": "Gebruikersnaam",
    "User name (if the broker wants one)": "Gebruikersnaam (als de broker er een vraagt)",
    "Password": "Wachtwoord",
    "Password (unchanged)": "Wachtwoord (ongewijzigd)",
    "Base topic": "Basistopic",
    "The base topic is zigbee2mqtt's mqtt.base_topic setting; usually zigbee2mqtt. The password is sent unencrypted, so only use this on your own network.":
        "Het basistopic is de instelling mqtt.base_topic van zigbee2mqtt; meestal zigbee2mqtt. Het wachtwoord gaat onversleuteld over het netwerk, dus gebruik dit alleen op je eigen netwerk.",
    "The MQTT broker did not respond": "De MQTT-broker reageert niet",
    "Lost the connection to the MQTT broker": "Verbinding met de MQTT-broker verloren",
    "The MQTT broker closed the connection": "De MQTT-broker heeft de verbinding verbroken",
    "The MQTT broker sent something unexpected": "Onverwacht antwoord van de MQTT-broker",
    "The MQTT broker did not accept the user name or password": "De MQTT-broker accepteert de gebruikersnaam of het wachtwoord niet",
    "The MQTT broker did not allow this user to connect": "De MQTT-broker laat deze gebruiker niet verbinden",
    "The MQTT broker refused the connection (%1)": "De MQTT-broker weigert de verbinding (%1)",
    "The MQTT broker did not allow this user to subscribe": "De MQTT-broker laat deze gebruiker niet abonneren",
    "Whole home": "Hele huis",
    "Schedules": "Schema's",
    "Delete": "Verwijderen",
    "Deleting schedule": "Schema verwijderen",
    "Time": "Tijd",
    "Days": "Dagen",
    "Without days, it runs once.": "Zonder dagen gaat hij één keer af.",
    "This schedule doesn't start at a fixed time; change it in the Hue app.":
        "Dit schema begint niet op een vaste tijd; pas het aan in de Hue-app.",
    "Not working: %1": "Werkt niet: %1",
    "No schedules": "Geen schema's",
    "Wake up, go to sleep and other automations are set up in the Hue app":
        "Wakker worden, gaan slapen en andere automatiseringen stel je in met de Hue-app",
    "Wake up": "Wakker worden",
    "Go to sleep": "Gaan slapen",
    "Timer": "Timer",
    "Lights on a schedule": "Lampen volgens schema",
    "Natural light": "Natuurlijk licht",
    "Coming home": "Thuiskomen",
    "Leaving home": "Weggaan",
    "Once": "Eenmalig",
    "Every day": "Elke dag",
    "Weekdays": "Doordeweeks",
    "Weekends": "In het weekend",
    "Sunrise": "Zonsopkomst",
    "Sunset": "Zonsondergang",
    "%1 + %n min": ["%1 + %n min", "%1 + %n min"],
    "%1 − %n min": ["%1 − %n min", "%1 − %n min"],
    "%n s": ["%n s", "%n s"],
    "%n min": ["%n min", "%n min"],
    "%n h": ["%n u", "%n u"],
    "%1 h %2 min": "%1 u %2 min",
    "%1 to %2": "%1 tot %2",
    # Only on the desktop.
    "Cancel": "Annuleren",
    "Click to switch to a scene; right-click to change it": "Klik om naar een scène te gaan; rechtsklik om hem te wijzigen",
    "Right-click to change or delete": "Rechtsklik om te wijzigen of te verwijderen",
    "Forgetting only removes the bridge from this computer. To revoke the app's access, remove \"%1\" in the Hue app.":
        "Vergeten verwijdert de bridge alleen van deze computer. Om de app de toegang te ontnemen, verwijder je \"%1\" in de Hue-app.",
    "Connect this computer to the same network as your Hue bridge.": "Verbind deze computer met hetzelfde netwerk als je Hue bridge.",
    "Connect": "Verbinden",
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
    root = Path(__file__).resolve().parent.parent
    # The phone's and the desktop's; their strings mostly the same.
    for folder, name in ((root / "app" / "translations", "harbour-lampje"),
                         (root / "desktop" / "translations", "lampje")):
        fill(folder / f"{name}-en.ts", EN, "en")
        missing = fill(folder / f"{name}-nl.ts", NL, "nl")
        for source in missing:
            print(f"nl ({name}): not translated: {source!r}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
