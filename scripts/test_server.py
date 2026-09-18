import asyncio
import websockets
import json

PORT = 8081


async def handle_connection(websocket):
    print("🟢 Client connected")

    try:
        async for message in websocket:
            print(f"📩 Received: {message}")
            request = json.loads(message)

            if request.get("request") == "load_root":
                print("📦 LoadRoot request received")
                response = {
                    "response": "root_response",
                    "root": {
                        "name": "TOP",
                        "child_id": 0,
                        "model_name": "TOP",
                        "design_ref": {
                            "db_id": 1,
                            "library_id": 1,
                            "design_id": 1
                        },
                        "has_terms": True,
                        "has_primitives": True,
                        "has_instances": True,
                        "has_nets": True
                    }
                }
                print(f"📤 Sent: {json.dumps(response)}")
                await websocket.send(json.dumps(response))

                # Demo diagnosis push: a real backend (e.g. a kepler-formal/
                # naja-scope adapter) would send this asynchronously whenever
                # a diagnosis run completes, not just after load_root.
                diagnosis = {
                    "response": "diagnosis_response",
                    "items": [
                        {
                            "kind": "instance",
                            "path": ["U1"],
                            "severity": "error",
                            "message": "kepler-formal: SEC counterexample touches this instance",
                            "source": "kepler-formal"
                        },
                        {
                            "kind": "net",
                            "path": [],
                            "terminal": "A",
                            "severity": "warning",
                            "message": "naja-scope: fan-out changed after RTL edit",
                            "source": "naja-scope"
                        }
                    ]
                }
                print(f"📤 Sent: {json.dumps(diagnosis)}")
                await websocket.send(json.dumps(diagnosis))
            elif request.get("request") in {"load_instances", "load_primitives"}:
                gui_id = request.get("gui_id", 0)
                print(f"🔍 Load instances/primitives for gui_id: {gui_id}")

                design_ref = request.get("design_ref") or {
                    "db_id": 1,
                    "library_id": 1,
                    "design_id": 1
                }

                response = {
                    "response": "instances_response" if request.get("request") == "load_instances" else "primitives_response",
                    "gui_id": gui_id,
                    "children": [
                        {
                            "name": "U1",
                            "child_id": 101,
                            "model_name": "child_model_1",
                            "design_ref": design_ref,
                            "has_terms": True,
                            "has_primitives": False,
                            "has_instances": False,
                            "has_nets": False
                        },
                        {
                            "name": "U2",
                            "child_id": 102,
                            "model_name": "child_model_2",
                            "design_ref": design_ref,
                            "has_terms": True,
                            "has_primitives": False,
                            "has_instances": True,
                            "has_nets": True
                        }
                    ]
                }
                print(f"📤 Sent: {json.dumps(response)}")
                await websocket.send(json.dumps(response))
                print(f"📤 Sent children for gui_id {gui_id}")
            elif request.get("request") == "load_terms":
                gui_id = request.get("gui_id", 0)
                print(f"🔍 Load terms for gui_id: {gui_id}")
                response = {
                    "response": "terms_response",
                    "gui_id": gui_id,
                    "children": [
                        {"name": "A", "child_id": 1, "direction": 0, "msb": None, "lsb": None},
                        {"name": "B", "child_id": 2, "direction": 1, "msb": None, "lsb": None},
                        {"name": "BUS", "child_id": 3, "direction": 2, "msb": 3, "lsb": 0}
                    ]
                }
                print(f"📤 Sent: {json.dumps(response)}")
                await websocket.send(json.dumps(response))
            elif request.get("request") == "load_nets":
                gui_id = request.get("gui_id", 0)
                print(f"🔍 Load nets for gui_id: {gui_id}")
                response = {
                    "response": "nets_response",
                    "gui_id": gui_id,
                    "children": [
                        {"name": "n1", "msb": None, "lsb": None},
                        {"name": "NBUS", "msb": 3, "lsb": 0}
                    ]
                }
                print(f"📤 Sent: {json.dumps(response)}")
                await websocket.send(json.dumps(response))
            elif request.get("request") == "load_equipotential":
                response = {
                    "response": "equipotential_response",
                    "terms": [],
                    "occurrences": []
                }
                print(f"📤 Sent: {json.dumps(response)}")
                await websocket.send(json.dumps(response))

    except websockets.exceptions.ConnectionClosed as e:
        print(f"🔴 Client disconnected: {e}")


async def main():
    print(f"🚀 WebSocket server starting on ws://localhost:{PORT}")
    async with websockets.serve(handle_connection, "localhost", PORT):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())
