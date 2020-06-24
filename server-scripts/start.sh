nohup python3 /home/pi/aqm/data-collector.py > /dev/null 2>&1 &
echo $! > data-collector.pid

echo "Data collector started."
