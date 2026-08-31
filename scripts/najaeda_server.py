from glob import glob
from najaeda import netlist, naja
import argparse
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

def has_visible_primitive_instances(design):
    return any(not instance.getModel().isAssign()
               for instance in design.getPrimitiveInstances())

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
        "has_primitives": has_visible_primitive_instances(model),
        "has_instances": model.hasNonPrimitiveInstances(),
    }

def direction_to_int(direction):
    if direction == naja.SNLTerm.Direction.Input:
        return 0
    elif direction == naja.SNLTerm.Direction.Output:
        return 1
    else:
        return 2


async def send_error(websocket, response_type, gui_id=0):
    await websocket.send(json.dumps({
        "response": response_type,
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
                    send_error(websocket, "root_response")
                else:
                    await websocket.send(json.dumps({
                        "response": "root_response",
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
                        "gui_id": gui_id,
                        "instance": {
                            "design_ref": {
                                "db_id": design.getDB().getID(),
                                "library_id": design.getLibrary().getID(),
                                "design_id": design.getID(),
                            },
                            "has_terms": design.hasTerms(),
                            "has_primitives": has_visible_primitive_instances(design),
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
                        if model.isAssign():
                            continue
                        children.append(serialize_model(model, instance.getID(), instance.getName()))

                    response_type = req_type.replace("load_", "") + "_response"
                    await websocket.send(json.dumps({
                        "response": response_type,
                        "gui_id": gui_id,
                        "children": children
                    }))
                elif req_type == "load_terms":
                    terms = [
                        { "name": term.getName(),
                          "child_id": term.getID(),
                          "direction": direction_to_int(term.getDirection()),
                          "msb": term.getMSB() if isinstance(term, naja.SNLBusTerm) else None,
                          "lsb": term.getLSB() if isinstance(term, naja.SNLBusTerm) else None,
                        } for term in design.getTerms()
                    ]
                    await websocket.send(json.dumps({
                        "response": "terms_response",
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
                    term = design.getTermByID(term_id)
                    print(f"🔍 Term resolved: {term}")
                    if bit is not None:
                        if not isinstance(term, naja.SNLBusTerm):
                            print(f"⚠️ Term is not a bus term but bit {bit} was specified")
                            await send_error(websocket, "equipotential_response")
                            continue
                        start_point = term.getBusTermBit(bit)
                    else:
                        start_point = term
                else:
                    design = path.getModel()
                    term = design.getTermByID(term_id)
                    instance = path.getTailInstance()
                    if bit is not None:
                        if not isinstance(term, naja.SNLBusTerm):
                            print(f"⚠️ Term is not a bus term but bit {bit} was specified")
                            await send_error(websocket, "equipotential_response")
                            continue
                        term = term.getBusTermBit(bit)
                    print(f"🔍 Term resolved: {term}")
                    inst_term = instance.getInstTerm(term)
                    print(f"🔗 Instance Term: {inst_term}")
                    head_path = path.getHeadPath()
                    start_point = naja.SNLOccurrence(head_path, inst_term)
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
                        "name": term.getName(),
                        "direction": direction_to_int(term.getDirection()),
                        "bit": term.getBit() if isinstance(term, naja.SNLBusTermBit) else None
                    })
                for term in equipotential.getTerms():
                    terms.append({
                        "name": term.getName(),
                        "child_id": term.getID(),
                        "direction": direction_to_int(term.getDirection()),
                        "bit": term.getBit() if isinstance(term, naja.SNLBusTermBit) else None
                    })
                await websocket.send(json.dumps({
                    "response": "equipotential_response",
                    "occurrences": occurrences,
                    "terms": terms
                }))
            else:
                print(f"⚠️ Unknown request type: {req_type}")

    except websockets.exceptions.ConnectionClosed as e:
        print(f"🔴 Client disconnected: {e}")


async def main():
    print(f"🚀 WebSocket server starting on ws://localhost:{PORT}")
    async with websockets.serve(handle_connection, "localhost", PORT):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="najaeda WebSocket Server")
    parser.add_argument("--port", type=int, default=8081,
                        help="Port to run the websocket server on (default: 8081)")
    parser.add_argument("--xilinx", action="store_true",
                        help="Load Xilinx primitives")
    parser.add_argument("--allow_unknown_designs", action="store_true",
                        help="Allow unknown designs when loading the design.")
    parser.add_argument("--liberty", nargs="*", help="List of liberty files to load")
    parser.add_argument("--verilog", type=str,
                        help="Verilog netlist to load")
    args = parser.parse_args()

    PORT = args.port

    if args.xilinx:
        print("📦 Loading Xilinx primitives")
        netlist.load_primitives('xilinx')

    # Load the liberty libraries and the Verilog design
    if args.liberty:
        #if arg contains *, expand to list of files
        expanded_liberty_files = []
        for lib in args.liberty:
            if '*' in lib:
                expanded_liberty_files.extend(glob.glob(lib))
            else:
                expanded_liberty_files.append(lib)
        for lib in expanded_liberty_files:
            print(f"📚 Loading liberty file: {lib}")
            netlist.load_liberty(lib)

    if not args.verilog:
        print("❌ No Verilog file specified. Use --verilog to provide a netlist.")
        exit(1)
    else:
        print(f"📄 Loading Verilog netlist: {args.verilog}")
        config = netlist.VerilogConfig()
        config.allow_unknown_designs = args.allow_unknown_designs
        top = netlist.load_verilog(args.verilog, config=config)
        print(f"✅ Design loaded: {top.get_name()}")

    asyncio.run(main())
