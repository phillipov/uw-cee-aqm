sudo kill "$(< data-collector.pid)"
rm data-collector.pid

echo "Data collector stopped."
