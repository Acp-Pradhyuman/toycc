# toycc online compiler

A small web frontend that compiles `.tc` source to x86_64 assembly, assembles/links it with `nasm` + `ld`, runs the resulting binary in a sandbox, and shows the output in a Monaco-powered editor.

**Stack:** Python Flask backend + vanilla JS frontend + `firejail` for sandboxing. Stateless — nothing saved between requests.

## Local testing (without Docker)

```bash
# From repo root, build the compiler first
make

# Install deps (one time)
sudo apt install -y nasm binutils firejail python3-flask

# Run the server
TOYCC_BIN=$(pwd)/build/main PORT=7860 python3 web/server.py

# Open http://localhost:7860
```

## Local testing with Docker

```bash
# Build from repo root — the Dockerfile expects that context
docker build -t toycc-web -f web/Dockerfile .

# Run
docker run --rm -p 7860:7860 toycc-web

# Open http://localhost:7860
```

**Note on firejail in Docker:** firejail needs SUID root, which the apt package sets up. However, some Docker configurations disable SUID. If `firejail` fails, the server falls back to bare subprocess execution with a timeout. That's fine for a trusted demo but not for hostile input — don't disable firejail on public deployments.

## Deploy to HuggingFace Spaces (free, no credit card)

HuggingFace Spaces is a free Docker-friendly hosting service. Always-on CPU tier, no sleep, no credit card.

### Steps

1. **Sign up / sign in** at https://huggingface.co

2. **Create a new Space:**
   - Go to https://huggingface.co/new-space
   - Owner: your username
   - Space name: `toycc` (or anything)
   - License: MIT (matches the repo)
   - **Space SDK: Docker → Blank**
   - Hardware: **CPU basic · FREE**
   - Public

3. **Push your code.** The Space comes with a git repo. Clone it, copy the toycc source plus the `web/` directory in, and push. HuggingFace expects the **Dockerfile at the root of the Space**, so we need to move it:

   ```bash
   # After creating the space, HuggingFace gives you a URL like
   # https://huggingface.co/spaces/<your-user>/toycc

   git clone https://huggingface.co/spaces/<your-user>/toycc hf-space
   cd hf-space

   # Copy the whole toycc repo into this clone (it'll be the build context)
   rsync -av --exclude='.git' --exclude='hf-space' /path/to/toycc/ .

   # The Dockerfile needs to be at the root for HuggingFace to find it.
   # The included web/Dockerfile expects the repo as context, so move it up:
   cp web/Dockerfile ./Dockerfile

   # Commit + push (you'll need the HF access token when prompted for password)
   git add .
   git commit -m "Initial toycc deploy"
   git push
   ```

4. **Wait for the build.** HuggingFace Spaces builds your Docker image server-side. First build takes ~3-5 min. The Space page shows build logs live. When it's green, visit the URL — you're online.

### Getting an HF access token (needed once for `git push`)

1. https://huggingface.co/settings/tokens
2. "New token" → type "write" → copy it.
3. When `git push` prompts for password, paste the token.

## Deploy to Render (alternative)

Render's free tier works too. Same Docker image, different process:

1. New → Web Service → "Build and deploy from a Git repository".
2. Point it at your GitHub fork of toycc.
3. Environment: Docker. Root directory: `.`. Dockerfile path: `./web/Dockerfile`.
4. Plan: Free.

Caveat: Render's free tier sleeps after 15 min of inactivity. First request after sleep takes ~30s-1min to cold-start. OK for demos, annoying for daily use.

## What's actually running

Every `/compile` request:
1. Writes the source to a fresh `tempfile.TemporaryDirectory`.
2. Invokes `toycc <src> <out_base>`. toycc itself shells out to `nasm` + `ld` to produce the binary.
3. Reads the `.asm` to show in the Assembly tab.
4. Runs the binary under `firejail --net=none --rlimit-*` with a 5-second wall-clock timeout.
5. Returns stdout / stderr / exit code / asm as JSON.

## Security notes

- **Why firejail:** toycc's codegen currently only emits `write`/`exit` syscalls, but nothing structurally prevents a future change from emitting others. firejail blocks network, caps memory, caps CPU time, caps file size.
- **Resource limits** are tuned for tiny programs. If you add a feature that needs more, edit `RUN_TIMEOUT_SEC` and the `--rlimit-*` flags in `web/server.py`.
- **Source size cap:** 64KB. Tunable via `MAX_SOURCE_LEN`.
- **Stateless:** no database, no logs of user code. Each request is a self-contained transaction.
