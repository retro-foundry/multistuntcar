# WebRTC signaling server (Cloudflare Workers)

Small WebRTC signaling service for the web multiplayer game. Runs on Cloudflare Workers with Durable Objects. One host and one guest connect to the same room and exchange WebRTC offers/answers and ICE candidates.

## Prerequisites

- [Node.js](https://nodejs.org/) (LTS)
- A [Cloudflare](https://cloudflare.com) account

## One-time setup

1. **Create the project and install Wrangler**

   ```bash
   npm init -y
   npm install -D wrangler
   ```

2. **Log in to Cloudflare**

   ```bash
   npx wrangler login
   ```

3. **Configure your deployment** (optional)

   Edit `wrangler.jsonc`:

   - `vars.APP_ORIGIN` – origin of the game (e.g. `https://your-username.github.io` or `https://yourgame.com`). Only this origin is allowed to call the API.
   - `vars.SIGNAL_HOST` – hostname of the deployed worker (e.g. `tiny-signal.your-subdomain.workers.dev`). Must match the worker’s URL after deploy.

4. **Set the shared secret**

   This is used to sign session tokens. Pick a long random string and store it as a secret:

   ```bash
   npx wrangler secret put SIGNAL_SHARED_SECRET
   ```

   Enter the secret when prompted (it is not stored in the repo).

## Deploy

```bash
npx wrangler deploy
```

After deploy, note the worker URL (e.g. `https://tiny-signal.<your-subdomain>.workers.dev`). The web game’s template uses a built-in signal URL; to use your own, change `SIGNAL_BASE` in `package/template.html` (and rebuild the web app) to your worker’s base URL.

## Summary

| Step              | Command |
|-------------------|--------|
| Install deps      | `npm install -D wrangler` |
| Login             | `npx wrangler login` |
| Set secret        | `npx wrangler secret put SIGNAL_SHARED_SECRET` |
| Deploy            | `npx wrangler deploy` |
