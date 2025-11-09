from najaeda import netlist, naja
import asyncio
import websockets
import json

PORT = 8081

def get_design_ref(ref_msg):
    if not ref_msg:
        return None
    return (
        ref_msg.get("db_id"),
        ref_msg.get("library_id"),
        ref_msg.get("design_id"),
    )

def get_path(top, instance_ids):
    current = top
    path = []
    naja_path = naja.SNLPath()
    for inst_id in instance_ids:
        instance = current.getInstanceByID(inst_id)
        if not instance:
            return None
        current = instance.getModel()
        naja_path = naja.SNLPath(naja_path, instance)
    return naja_path

def serialize_model(model, child_id, name):
    return {
        "name": name,
        "child_id": child_id,
        "model_name": model.getName(),
        "design_ref": {
            "db_id": model.getDB().getID(),
            "library_id": model.getLibrary().getID(),
            "design_id": model.getID(),
        },
        "has_terms": model.hasTerms(),
        "has_primitives": model.hasPrimitiveInstances(),
        "has_instances": model.hasNonPrimitiveInstances(),
    }


async def send_error(websocket, response_type, gui_id=0):
    await websocket.send(json.dumps({
        "response": response_type,
        "found": False,
        "gui_id": gui_id
    }))

async def handle_connection(websocket):
    print("🟢 Client connected")
    try:
        async for message in websocket:
            print(f"📩 Received: {message}")
            request = json.loads(message)
            req_type = request.get("request")
            design_ref_message = request.get("design_ref")
            gui_id = request.get("gui_id", 0)

            u = naja.NLUniverse.get()

            if req_type == "load_root":
                print("📦 LoadRoot request received")
                top = u.getTopDesign()

                if not top:
                    await websocket.send(json.dumps({
                        "response": "root_response",
                        "found": False
                    }))
                    continue

                await websocket.send(json.dumps({
                    "response": "root_response",
                    "found": True,
                    "root": serialize_model(top, 0, top.getName())
                }))

            elif req_type in {"load_instance", "load_primitives", "load_instances", "load_terms"}:
                if not design_ref_message:
                    print("⚠️ Missing design_ref in request")
                    continue

                design_ref = get_design_ref(design_ref_message)
                print(f"🔍 {req_type} for: {design_ref}")
                design = u.getSNLDesign(design_ref)

                if not design:
                    await send_error(websocket, f"{req_type}_response", gui_id)
                    continue

                if req_type == "load_instance":
                    await websocket.send(json.dumps({
                        "response": "instance_response",
                        "found": True,
                        "gui_id": gui_id,
                        "instance": {
                            "design_ref": {
                                "db_id": design.getDB().getID(),
                                "library_id": design.getLibrary().getID(),
                                "design_id": design.getID(),
                            },
                            "has_terms": design.hasTerms(),
                            "has_primitives": design.hasPrimitiveInstances(),
                            "has_instances": design.hasNonPrimitiveInstances()
                        }
                    }))

                elif req_type in {"load_primitives", "load_instances"}:
                    children = []
                    instances = (design.getPrimitiveInstances()
                                 if req_type == "load_primitives"
                                 else design.getNonPrimitiveInstances())

                    for instance in instances:
                        model = instance.getModel()
                        children.append(serialize_model(model, instance.getID(), instance.getName()))

                    response_type = req_type.replace("load_", "") + "_response"
                    await websocket.send(json.dumps({
                        "response": response_type,
                        "found": True,
                        "gui_id": gui_id,
                        "children": children
                    }))
                elif req_type == "load_terms":
                    terms = [
                        { "name": term.getName(),
                          "child_id": term.getID(),
                          "direction": 0 if term.getDirection() == naja.SNLTerm.Direction.Input
                                        else 1 if term.getDirection() == naja.SNLTerm.Direction.Output
                                        else 2,
                          "msb": term.getMSB() if isinstance(term, naja.SNLBusTerm) else None,
                          "lsb": term.getLSB() if isinstance(term, naja.SNLBusTerm) else None,
                        } for term in design.getTerms()
                    ]
                    await websocket.send(json.dumps({
                        "response": "terms_response",
                        "found": True,
                        "gui_id": gui_id,
                        "children": terms
                    }))
                
            elif req_type == "load_equipotential":
                path_ids = request.get("path", [])
                term_id = request.get("term_id", {})
                bit = request.get("bit", None)
                print(f"⚡ LoadEquipotential request for path: {path_ids} and term_id: {term_id} bit: {bit}")
                path = get_path(u.getTopDesign(), path_ids)
                print(f"🏞️ Resolved path: {path}")
                start_point = None
                design = None
                if path.empty():
                    design = u.getTopDesign()
                    start_point = design.getTermByID(term_id)
                else:
                    design = path.getModel()
                    term = design.getTermByID(term_id)
                    start_point = naja.SNLInstTermOccurrence(path, term)
                print(f"🔍 Start point: {start_point}")
                equipotential = naja.SNLEquipotential(start_point)
                occurrences = []
                terms = []
                for occ in equipotential.getInstTermOccurrences():
                    path = [[inst.getName(), inst.getID()] for inst in occ.getPath().getInstances()]
                    path.append([occ.getInstTerm().getInstance().getName(), occ.getInstTerm().getInstance().getID()])
                    instTerm = occ.getInstTerm()
                    term = instTerm.getBitTerm()
                    occurrences.append({
                        "path": path,
                        "term_id": term.getID(),
                    })
                for term in equipotential.getTerms():
                    terms.append({
                        "name": term.getName(),
                        "child_id": term.getID(),
                        "direction": 0 if term.getDirection() == naja.SNLTerm.Direction.Input
                                      else 1 if term.getDirection() == naja.SNLTerm.Direction.Output
                                      else 2,
                        "bit": None
                    })
                await websocket.send(json.dumps({
                    "response": "equipotential_response",
                    "found": True,
                    "gui_id": gui_id,
                    "occurrences": occurrences,
                    "terms": terms
                }))

                #if bit is not None:
                #    #term_occurrence = naja.SNLBusTermOccurrence(path, term, bit)
                #if term_id. get("type") == "term":
                #    term = path.getDesign().getTermByID(term_id.get("child_id"))
                #    term_occurrence = naja.SNLTermOccurrence(path, term)
                #term_occurrence = naja.SNLTermOccurrence(path, )
                #bit = u.getTermBitOccurrence(term_id_msg)
                # Equipotential loading not implemented yet
                await send_error(websocket, "equipotential_response", gui_id)

            else:
                print(f"⚠️ Unknown request type: {req_type}")

    except websockets.exceptions.ConnectionClosed as e:
        print(f"🔴 Client disconnected: {e}")


async def main():
    print(f"🚀 WebSocket server starting on ws://localhost:{PORT}")
    async with websockets.serve(handle_connection, "localhost", PORT):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    # Load the liberty libraries and the Verilog design
    liberty_files = [
        'NangateOpenCellLibrary_typical.lib',
        'fakeram45_64x32.lib',
    ]
    netlist.load_liberty(liberty_files)

    top = netlist.load_verilog('tinyrocket.v')
    print(f"✅ Design loaded: {top.get_name()}")

    asyncio.run(main())