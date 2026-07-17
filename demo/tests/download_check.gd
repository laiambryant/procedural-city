# Exercises the CLI resolution chain including the GitHub release download.
# Run headless (hide any bundled binary first to reach the network path):
#   godot --headless --path demo --script res://tests/download_check.gd
extends SceneTree


func _init() -> void:
	_run()


func _run() -> void:
	# TLS default certificates only become available once the main loop has
	# started; calling straight from _init() would fail the handshake.
	await process_frame
	var runner := GoplacementxRunner.new()
	var path := runner.ensure_binary("", true)
	print("download_check: resolved=", path if not path.is_empty() else "<none>")

	var gpu_runner := GoplacementxRunner.new()
	gpu_runner.cli_kind = GoplacementxRunner.CLI_GPUDISPLACEMENTX
	var gpu_path := gpu_runner.ensure_binary("", true)
	var gpu_label := gpu_path if not gpu_path.is_empty() else "<none> (expected until the first gpudisplacementx release)"
	print("download_check: gpu resolved=", gpu_label)
	quit(0)
