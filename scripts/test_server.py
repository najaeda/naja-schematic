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
                await websocket.send(json.dumps({
                    "response": "root_loaded",
                    "root": {
                        "name": "TOP",
                        "instance_id": 0,
                        "model_name": "TOP",
                        "design_ref": {
                            "db_id": 1,
                            "library_id": 1,
                            "design_id": 1
                        },
                        "has_children": True
                    }
                }))
            elif request.get("request") == "load_children":
                node = request.get("node")
                print(f"🔍 Load children for: {node}")

                # Create DesignRef (from the original request or hardcoded for demo)
                design_ref = {
                    "db_id": 1,
                    "library_id": 1,
                    "design_id": 1
                }

                # Create children compatible with NetlistTreeNodeJson
                await websocket.send(json.dumps({
                    "response": "children_loaded",
                    "node_gui_id": request.get("node_gui_id"),
                    "children": [
                        {
                            "name": f"{node}_Child1",
                            "instance_id": 101,
                            "model_name": "child_model_1",
                            "design_ref": design_ref,
                            "has_children": False
                        },
                        {
                            "name": f"{node}_Child2",
                            "instance_id": 102,
                            "model_name": "child_model_2",
                            "design_ref": design_ref,
                            "has_children": True
                        }
                    ]}))

                print(f"📤 Sent children for {node}")

    except websockets.exceptions.ConnectionClosed as e:
        print(f"🔴 Client disconnected: {e}")


async def main():
    print(f"🚀 WebSocket server starting on ws://localhost:{PORT}")
    async with websockets.serve(handle_connection, "localhost", PORT):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())
