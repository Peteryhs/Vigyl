import json

import requests
import urllib3
from flask import Flask, jsonify, request
import threading
import time


app = Flask(__name__)

# Config
API_TOKEN = ""
PORTAINER_TOKEN = ""
DEEPSEEK_API_KEY = ""
DEEPSEEK_URL = ""

# Main server - runs Portainer and PCP
MAIN_SERVER = {
    "name": "",
    "ip": "",
    "endpoint_id": ""
}

# Main + Other Servers 
SERVERS = [
    MAIN_SERVER,
    {"name": "", "ip": "", "endpoint_id": "" }
]

PORTAINER_URL = f"https://{MAIN_SERVER['ip']}:9443/api"

ENDPOINT_NAMES = {s["endpoint_id"]: s["name"] for s in SERVERS}

active_alerts = {}
latest_hardware_stats = {}
previous_net_stats = {}


# Maps Uptime Kuma Service Names to Actual Docker Containers
CONTAINER_MAP = {
    "Immich": {
        "endpoint_id": 3,  # casa
        "container_name": "immich_server",
    },
    "Memos": {
        "endpoint_id": 3,  
        "container_name": "memos",
    },
    "OpenWebUI": {
        "endpoint_id": 1,  # rn225
        "container_name": "open-webui",
    },
    "Nextcloud": {
        "endpoint_id": 3,  
        "container_name": "nextcloud-aio-nextcloud",
    },
    "Portainer": {
        "endpoint_id": 1,  
        "container_name": "portainer",
    },
}

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# Begin AI Generated Code
# Google Gemini 3.1 Pro

def get_container_status(endpoint_id, container_name):
    """
    Fetches the container's status details from Portainer.
    """
    url = f"{PORTAINER_URL}/endpoints/{endpoint_id}/docker/containers/{container_name}/json"
    headers = {"X-API-Key": PORTAINER_TOKEN}

    try:
        response = requests.get(url, headers=headers, timeout=5, verify=False)
        response.raise_for_status()
        container_info = response.json()

        state = container_info.get("State", {})
        status = state.get("Status", "unknown")
        running = state.get("Running", False)
        error = state.get("Error", "")
        exit_code = state.get("ExitCode", 0)

        return {"status": status, "running": running, "error": error, "exit_code": exit_code}
    except requests.exceptions.RequestException as e:
        print(f"Failed to fetch status for {container_name}: {e}")
        return None


def get_container_logs(endpoint_id, container_name, lines=20):
    """
    Fetches container logs from Portainer.
    """
    url = f"{PORTAINER_URL}/endpoints/{endpoint_id}/docker/containers/{container_name}/logs"
    params = {"stdout": 1, "stderr": 1, "tail": lines}
    headers = {"X-API-Key": PORTAINER_TOKEN}

    try:
        response = requests.get(url, params=params, headers=headers, timeout=5, verify=False)
        response.raise_for_status()

        raw_logs = response.content
        if not raw_logs.strip():
            return "No logs found or container is empty."

        # Strip Docker's 8-byte stream headers from each log line
        cleaned_lines = []
        i = 0
        while i < len(raw_logs):
            if i + 8 <= len(raw_logs):
                # Read the 8-byte header: bytes 4-7 are the payload size (big-endian)
                size = int.from_bytes(raw_logs[i+4:i+8], 'big')
                payload = raw_logs[i+8:i+8+size].decode('utf-8', errors='replace').strip()
                if payload:
                    cleaned_lines.append(payload)
                i += 8 + size
            else:
                break

        return "\n".join(cleaned_lines)

    except requests.exceptions.RequestException as e:
        print(f"Failed to fetch logs for {container_name}: {e}")
        return None


def fetch_pcp_stats(server_ip):
    """
    Fetches and calculates CPU (Load), RAM (%), Disk (%), and Network (KB/s) from a PCP pmproxy endpoint.
    """
    url = f"http://{server_ip}:44322/pmapi/fetch"
    params = {"names": "kernel.all.load,mem.physmem,mem.util.used,filesys.capacity,filesys.used,network.interface.in.bytes,network.interface.out.bytes"}

    try:
        response = requests.get(url, params=params, timeout=3)
        response.raise_for_status()
        data = response.json()

        cpu_data = next(item for item in data["values"] if item["name"] == "kernel.all.load")
        cpu_load = round(cpu_data["instances"][0]["value"], 2)

        ram_total_data = next(item for item in data["values"] if item["name"] == "mem.physmem")
        ram_used_data = next(item for item in data["values"] if item["name"] == "mem.util.used")

        ram_total = ram_total_data["instances"][0]["value"]
        ram_used = ram_used_data["instances"][0]["value"]
        ram_percent = round((ram_used / ram_total) * 100, 1)

        disk_total_data = next(item for item in data["values"] if item["name"] == "filesys.capacity")
        disk_used_data = next(item for item in data["values"] if item["name"] == "filesys.used")

        disk_total_kbytes = sum(inst["value"] for inst in disk_total_data["instances"])
        disk_used_kbytes = sum(inst["value"] for inst in disk_used_data["instances"])

        disk_percent = 0.0
        if disk_total_kbytes > 0:
            disk_percent = round((disk_used_kbytes / disk_total_kbytes) * 100, 1)

        # Network throughput (rate from counter deltas)
        net_in_kbps = 0.0
        net_out_kbps = 0.0

        net_in_data = next((item for item in data["values"] if item["name"] == "network.interface.in.bytes"), None)
        net_out_data = next((item for item in data["values"] if item["name"] == "network.interface.out.bytes"), None)

        if net_in_data and net_out_data:
            total_in = sum(inst["value"] for inst in net_in_data["instances"])
            total_out = sum(inst["value"] for inst in net_out_data["instances"])
            current_time = time.time()

            if server_ip in previous_net_stats:
                prev = previous_net_stats[server_ip]
                dt = current_time - prev["timestamp"]
                if dt > 0:
                    net_in_kbps = max(0.0, round((total_in - prev["in_bytes"]) / dt / 1024, 1))
                    net_out_kbps = max(0.0, round((total_out - prev["out_bytes"]) / dt / 1024, 1))

            previous_net_stats[server_ip] = {
                "timestamp": current_time,
                "in_bytes": total_in,
                "out_bytes": total_out
            }

        return {"status": "online", "cpu_load": cpu_load, "ram_pct": ram_percent, "disk_pct": disk_percent, "net_in_kbps": net_in_kbps, "net_out_kbps": net_out_kbps}

    except requests.exceptions.RequestException as e:
        print(f"Error fetching from {server_ip}: {e}")
        return {"status": "offline", "cpu_load": 0.0, "ram_pct": 0.0, "disk_pct": 0.0, "net_in_kbps": 0.0, "net_out_kbps": 0.0}

# End AI Generated Code

def summarize_with_ai(error_msg, logs):

    prompt = (
        f"A Docker container has gone down.\n"
        f"Error: {error_msg}\n"
        f"Docker Logs:\n{logs}\n\n"
        f"Summarize why the container went down in no more than 6 words."
    )

    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}"
    }
    payload = {
        "model": "deepseek-v4-flash",
        "messages": [
            {"role": "system", "content": "You are a concise server diagnostics assistant. Respond with only a short summary, no more than 6 words."},
            {"role": "user", "content": prompt}
        ],
        "stream": False
    }

    try:
        response = requests.post(DEEPSEEK_URL, headers=headers, json=payload, timeout=10)
        response.raise_for_status()
        data = response.json()
        return data["choices"][0]["message"]["content"].strip()
    except Exception as e:
        print(f"DeepSeek AI summarization failed: {e}")
        return "AI summary unavailable"


# Begin AI Generated Code
# Google Gemini 3.1 Pro

# Flask Backend
@app.route("/webhook/uptime-kuma", methods=["POST"])
def uptime_kuma_webhook():
    print("\n" + "=" * 40)
    print("NEW INCOMING REQUEST CAPTURED")
    print("=" * 40)

    # 1. Dump HTTP Headers
    print("\n--- HEADERS ---")
    for header, value in request.headers.items():
        print(f"{header}: {value}")

    # 2. Parse and dump raw JSON body
    data = request.get_json(silent=True)
    print("\n--- JSON BODY ---")
    if not data:
        print("[EMPTY OR INVALID JSON BODY]")
        print("=" * 40 + "\n")
        return jsonify({"status": "invalid_json"}), 400

    print(json.dumps(data, indent=2))
    print("-" * 40)

    monitor = data.get("monitor", {})
    heartbeat = data.get("heartbeat", {})

    monitor_name = monitor.get("name")
    status_code = heartbeat.get("status")  # 0 = Down, 1 = Up, 2 = Pending
    msg = data.get("msg", "")

    print(f"\nMonitor Name: {monitor_name}")
    print(f"Status: {'Down' if status_code == 0 else 'Up' if status_code == 1 else 'Pending'}")
    print(f"Message: {msg}")

    # 3. Match monitor name to Portainer container
    if monitor_name in CONTAINER_MAP:
        config = CONTAINER_MAP[monitor_name]
        endpoint_id = config["endpoint_id"]
        container_name = config["container_name"]

        print(f"\nCONTAINER MATCH FOUND IN MAP:")
        server_name = ENDPOINT_NAMES.get(endpoint_id, f"Unknown ID {endpoint_id}")
        print(f"   Server Name:        {server_name}")
        print(f"   Container Name:     {container_name}")

        # 4. Fetch the real-time Docker Container Status
        print(f"\nQuerying Portainer for container status...")
        status_info = get_container_status(endpoint_id, container_name)
        if status_info:
            print(f"\nReal-Time Docker Container Status:")
            print(f"   State:     {status_info.get('status', 'unknown').upper()}")
            print(f"   Running:   {status_info.get('running')}")
            if status_info.get("error"):
                print(f"   Error:     {status_info.get('error')}")
            print(f"   Exit Code: {status_info.get('exit_code')}")
        else:
            print(f"\n Could not retrieve container status from Portainer API.")

        # 5. Automatically fetch logs if the container status is not running or monitor is Down
        if status_code == 0 or (status_info and not status_info.get("running")):
            print(f"\n Fetching last 20 lines of container logs...")
            logs = get_container_logs(endpoint_id, container_name, lines=20)
            if logs:
                print("\n--- CONTAINER LOGS ---")
                print(logs)
                print("----------------------")
    else:
        print(f"\n Monitor '{monitor_name}' is not in CONTAINER_MAP. Skipping container query.")

    print("\n" + "=" * 40 + "\n")
    # Only keep alerts for services that are DOWN
    # When Uptime Kuma reports it's back up, remove the alert
    if status_code == 0:
        active_alerts[monitor_name] = {"status": status_code, "msg": msg}
    elif monitor_name in active_alerts:
        del active_alerts[monitor_name]
        print(f"  {monitor_name} is back up, clearing alert.")

    return jsonify({"status": "captured"}), 200

# End AI Generated Code

@app.route("/api/status", methods=["GET"])
def get_status():
    if request.headers.get("X-API-Key") == API_TOKEN:
        pass
    else:
        return jsonify({"error": "Unauthorized"}), 401

    status_report = []
    
    # Fetch hardware stats from all servers
    hardware = []
    for server in SERVERS:
        hw_status = fetch_pcp_stats(server["ip"])
        hardware.append({'Server_Name': server["name"], 'hw_status': hw_status})
    status_report.append({'hardware': hardware})
    
    for monitor_name, status in active_alerts.items():
        monitor_data = {
            'Monitor_Name': monitor_name, 
            'Status': status['status'], 
            'Message': status['msg']
        }
        
        # Protect against KeyError!
        if monitor_name in CONTAINER_MAP:
            config = CONTAINER_MAP[monitor_name]
            server_name = ENDPOINT_NAMES.get(config["endpoint_id"], str(config["endpoint_id"]))
            monitor_data['Server_Name'] = server_name
            status_info = get_container_status(config["endpoint_id"], config["container_name"])
            monitor_data['Container_Status'] = status_info
            
            if status['status'] == 0:
                # Cache logs and AI summary in active_alerts to prevent repeated API calls
                if 'logs' not in status:
                    status['logs'] = get_container_logs(config["endpoint_id"], config["container_name"], lines=20)
                if 'ai_summary' not in status:
                    status['ai_summary'] = summarize_with_ai(status['msg'], status['logs'])
                
                monitor_data['AI_Summary'] = status['ai_summary']
                
        status_report.append(monitor_data)
        
    return jsonify(status_report), 200

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5000)
