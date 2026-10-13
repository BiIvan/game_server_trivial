import json
import sys
from urllib.request import Request, urlopen

base = (sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:18080").rstrip("/")
def request(path, data=None, token=None):
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = Request(base + path, data=None if data is None else json.dumps(data).encode(), headers=headers)
    with urlopen(req, timeout=10) as response:
        return json.load(response)

maps = request("/api/v1/maps")
assert maps, "No maps"
map_id = maps[0]["id"]
map_data = request("/api/v1/maps/" + map_id)
assert map_data["lootTypes"] and all("value" in t for t in map_data["lootTypes"])
joined = request("/api/v1/game/join", {"userName": "API verification", "mapId": map_id})
state = request("/api/v1/game/state", token=joined["authToken"])
player = state["players"][str(joined["playerId"])]
assert player["bag"] == [] and player["score"] == 0
assert isinstance(state["lostObjects"], dict)
print("Map lootTypes, join, bag, score and lostObjects: OK")
