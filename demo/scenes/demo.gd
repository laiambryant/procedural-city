extends Node3D

## Minimal demo driver: wires the generator's signals to the console and kicks
## off a full generation at runtime. In the editor, use the inspector buttons on
## the ProcCityGenerator node instead.

@onready var generator: ProcCityGenerator = $ProcCityGenerator

func _ready() -> void:
	generator.generation_started.connect(_on_started)
	generator.generation_finished.connect(_on_finished)
	generator.generation_failed.connect(_on_failed)
	generator.all_finished.connect(_on_all_finished)
	generator.generate_all()

func _on_started(stage: String) -> void:
	print("[demo] started: ", stage)

func _on_finished(stage: String, path: String) -> void:
	print("[demo] finished: ", stage, "  ", path)

func _on_failed(stage: String, message: String) -> void:
	push_error("[demo] failed (%s): %s" % [stage, message])

func _on_all_finished() -> void:
	print("[demo] all finished")
