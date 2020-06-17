import paho.mqtt.client as mqtt
import sqlite3
import secrets

# mqtt generic callback: connected to broker
def on_connect(client, userdata, flags, rc):
    print("Connected (code " + str(rc) + ")")
    client.subscribe("sensors/data")

# mqtt generic callback: disconnected from broker
def on_disconnect(client, userdata, rc):
    print("Disconnected (code " + str(rc) + ")")

# mqtt generic callback: message recieved
def on_message(client, userdata, msg):
    print(msg.topic+" " + str(msg.payload))

# mqtt subscription callback: data received from sensor
def data_receive(client, userdata, msg):
    print("DATA: " + str(msg.payload))

sql_create_data_table =         """ CREATE TABLE IF NOT EXISTS sensor_data (
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
                                    ); """

# sqlite database setup
db_conn = sqlite3.connect("sensor-data.db")
db_cursor = db_conn.cursor()
db_cursor.execute(sql_create_data_table)

# mqtt client setup
client = mqtt.Client(client_id="database-client", userdata=db_cursor, clean_session=False)
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect
client.username_pw_set(secrets.username, secrets.password)
client.connect(secrets.host, secrets.port, keepalive=60)

client.message_callback_add("sensors/data", data_receive)

client.loop_forever()