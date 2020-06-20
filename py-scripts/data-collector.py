import paho.mqtt.client as mqtt
import sqlite3
import json
import secrets

sql_create_data_table = """ CREATE TABLE IF NOT EXISTS sensor_data (
                                sensor_id TEXT NOT NULL,
                                time TIMESTAMP NOT NULL,
                                temp REAL,
                                hum REAL,
                                pm_1_0 INTEGER,
                                pm_2_5 INTEGER,
                                pm_10_0 INTEGER,
                                CHECK(sensor_id <> ''),
                                CHECK(time <> ''),
                                PRIMARY KEY (sensor_id, time)
                            );"""

sql_insert_data = """   INSERT INTO sensor_data
                        VALUES (?, ?, ?, ?, ?, ?, ?);"""

# mqtt generic callback: connected to broker
def on_connect(client, db_conn, flags, rc):
    print("Connected (code " + str(rc) + ")")
    client.subscribe("sensors/data")

# mqtt generic callback: disconnected from broker
def on_disconnect(client, db_conn, rc):
    print("Disconnected (code " + str(rc) + ")")

# mqtt generic callback: message recieved
def on_message(client, db_conn, msg):
    print(msg.topic+" " + str(msg.payload))

# mqtt subscription callback: data received from sensor
def data_receive(client, db_conn, msg):
    jsonMsg = json.loads(msg.payload)
    
    record = (  jsonMsg.get("sensor_id"),
                jsonMsg.get("time"),
                jsonMsg.get("temp"),
                jsonMsg.get("hum"),
                jsonMsg.get("pm_1_0"),
                jsonMsg.get("pm_2_5"),
                jsonMsg.get("pm_10_0")
            )
    
    try:
        with db_conn:
            db_conn.execute(sql_insert_data, record)
    except sqlite3.Error:
        print("sqlite error occurred. error with data:")
        print(jsonMsg)

# sqlite database setup
db_conn = sqlite3.connect("sensor-data.db")
db_conn.execute(sql_create_data_table)

# mqtt client setup
client = mqtt.Client(client_id="database-client", userdata=db_conn, clean_session=False)
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect
client.username_pw_set(secrets.username, secrets.password)
client.connect(secrets.host, secrets.port, keepalive=60)

client.message_callback_add("sensors/data", data_receive)

client.loop_forever()