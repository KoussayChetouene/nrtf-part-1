import paho.mqtt.client as mqtt
import json, csv, os
from datetime import datetime

BROKER = "localhost"
PORT   = 1883

def on_connect(client, userdata, flags, rc):
    print("Connecte au broker MQTT !")
    client.subscribe("factory/#")
    print("En attente de donnees...")

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        topic = msg.topic
        timestamp = datetime.utcnow().isoformat()

        if topic == "factory/gas":
            row = {
                "received_at": timestamp,
                "device":      data.get("device", ""),
                "ts":          data.get("ts", ""),
                "gas_ppm":     data.get("gas_ppm", ""),
                "type":        data.get("type", "")
            }
            save_csv("gas_log.csv", row)
            print(f"[GAS] gas_ppm={row['gas_ppm']} ppm")

        elif topic == "factory/power":
            row = {
                "received_at": timestamp,
                "device":      data.get("device", ""),
                "ts":          data.get("ts", ""),
                "current_a":   abs(float(data.get("current_a", 0))),
                "voltage_v":   data.get("voltage_v", ""),
                "power_w":     abs(float(data.get("power_w", 0)))
            }
            save_csv("power_log.csv", row)
            print(f"[POWER] current={row['current_a']}A voltage={row['voltage_v']}V power={row['power_w']}W")

    except Exception as e:
        print(f"Erreur: {e}")

def save_csv(filename, data):
    file_exists = os.path.isfile(filename)
    with open(filename, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=data.keys())
        if not file_exists:
            writer.writeheader()
        writer.writerow(data)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, 60)
print("Demarrage du serveur...")
client.loop_forever()