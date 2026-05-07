"""
Thin HTTP wrapper around the toycc pipeline.

POST /compile  body: {"source": "..."}
  -> {
       "asm":       "... nasm assembly ...",
       "stdout":    "program stdout",
       "stderr":    "program stderr",
       "exit_code": int | null,
       "compile_stderr": "compiler error messages or empty",
       "compile_ok":    bool,
       "timed_out":     bool
     }

The compiler is invoked as a subprocess with a per-request temp directory,
so concurrent requests don't clobber each other. The generated binary is
executed under `firejail` with a 5s wall-clock timeout, a 64MB memory
cap, and no network. If firejail isn't available (e.g. local dev without
it installed), we fall back to bare subprocess with the same timeout.

Stateless: nothing persisted between requests.
"""
import json
import os
import shutil
import subprocess
import tempfile
from flask import Flask, request, jsonify, send_from_directory

APP_DIR        = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR     = os.path.join(APP_DIR, "static")
TOYCC_BIN      = os.environ.get("TOYCC_BIN", "/app/build/main")

# Resource limits for the user's program.
RUN_TIMEOUT_SEC = 5
MAX_SOURCE_LEN  = 64 * 1024  # 64KB of source is plenty for toycc programs

# Compile-step timeout (the compiler itself, not the output binary).
COMPILE_TIMEOUT_SEC = 10

app = Flask(__name__, static_folder=STATIC_DIR, static_url_path="")


def _have(cmd):
    return shutil.which(cmd) is not None


def _run_sandboxed(binary_path, workdir):
    """Execute the user's program with strict resource limits."""
    if _have("firejail"):
        cmd = [
            "firejail",
            "--quiet",
            "--private=" + workdir,
            "--net=none",
            "--rlimit-as=67108864",       # 64MB address space
            "--rlimit-cpu=2",             # 2 CPU seconds
            "--rlimit-nofile=8",
            "--rlimit-fsize=65536",       # 64KB max file write
            "./" + os.path.basename(binary_path),
        ]
    else:
        # Fallback: rely on timeout only. OK for local dev.
        cmd = [binary_path]

    try:
        proc = subprocess.run(
            cmd,
            cwd=workdir,
            capture_output=True,
            timeout=RUN_TIMEOUT_SEC,
        )
        return {
            "stdout":    proc.stdout.decode("utf-8", errors="replace"),
            "stderr":    proc.stderr.decode("utf-8", errors="replace"),
            "exit_code": proc.returncode,
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as e:
        return {
            "stdout":    (e.stdout or b"").decode("utf-8", errors="replace"),
            "stderr":    (e.stderr or b"").decode("utf-8", errors="replace"),
            "exit_code": None,
            "timed_out": True,
        }


@app.post("/compile")
def compile_endpoint():
    data = request.get_json(silent=True) or {}
    source = data.get("source", "")
    if not isinstance(source, str):
        return jsonify(error="source must be a string"), 400
    if len(source) > MAX_SOURCE_LEN:
        return jsonify(error=f"source too long (max {MAX_SOURCE_LEN} bytes)"), 400

    with tempfile.TemporaryDirectory(prefix="toycc-") as tmp:
        src_path  = os.path.join(tmp, "input.tc")
        out_base  = os.path.join(tmp, "generated")
        asm_path  = out_base + ".asm"
        bin_path  = out_base

        with open(src_path, "w") as f:
            f.write(source)

        # Compile step. toycc writes the .asm next to the output name we
        # give it, then shells out to nasm + ld to produce the binary.
        # It may leave debug output on stdout; we don't surface that in the
        # UI because it's parser-internal noise.
        try:
            proc = subprocess.run(
                [TOYCC_BIN, src_path, out_base],
                capture_output=True,
                timeout=COMPILE_TIMEOUT_SEC,
                cwd=tmp,
            )
        except subprocess.TimeoutExpired:
            return jsonify(
                compile_ok=False,
                compile_stderr="compiler timed out (possible infinite loop in "
                               "simulation)",
                asm="", stdout="", stderr="",
                exit_code=None, timed_out=True,
            )

        # Prefer stderr for diagnostics (error_at uses stderr with carets).
        # Some older error paths still go to stdout; concat both for display.
        compile_stderr = proc.stderr.decode("utf-8", errors="replace")
        compile_stdout = proc.stdout.decode("utf-8", errors="replace")

        # Heuristic: "error:" in either stream → compile failure.
        compile_failed = proc.returncode != 0 or "error:" in compile_stderr

        # Read the generated .asm if present, even on failure (useful
        # when nasm/ld fail downstream but asm was still emitted).
        asm = ""
        if os.path.isfile(asm_path):
            with open(asm_path) as f:
                asm = f.read()

        if compile_failed or not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
            # Some old error paths still print to stdout, not stderr. If
            # stderr is empty, surface whatever lines on stdout look like
            # errors so the user sees something useful.
            diag = compile_stderr
            if not diag.strip():
                diag = "\n".join(
                    line for line in compile_stdout.splitlines()
                    if "error" in line.lower() or "Error" in line
                )
            return jsonify(
                compile_ok=False,
                compile_stderr=diag,
                asm=asm,
                stdout="", stderr="",
                exit_code=None, timed_out=False,
            )

        # Run the binary.
        result = _run_sandboxed(bin_path, tmp)
        return jsonify(
            compile_ok=True,
            compile_stderr=compile_stderr,
            asm=asm,
            stdout=result["stdout"],
            stderr=result["stderr"],
            exit_code=result["exit_code"],
            timed_out=result["timed_out"],
        )


@app.get("/health")
def health():
    return jsonify(ok=True, toycc_exists=os.path.isfile(TOYCC_BIN))


@app.get("/")
def index():
    return send_from_directory(STATIC_DIR, "index.html")


if __name__ == "__main__":
    # HuggingFace Spaces defaults to 7860, others use PORT env var.
    port = int(os.environ.get("PORT", "7860"))
    app.run(host="0.0.0.0", port=port)
