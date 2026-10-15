# Deploy QuizForge on Render (5 minutes)

The Dockerfile and render.yaml are already at the repo root, so Render needs no extra config.

1. Unzip this folder. Open `web/index.html` and replace `YOUR-USERNAME` in the footer link with your GitHub username.
2. Create an EMPTY repo on github.com (no README), then in this folder run:
       git init
       git add .
       git commit -m "QuizForge"
       git branch -M main
       git remote add origin https://github.com/YOUR-USERNAME/quizforge.git
       git push -u origin main
   Make sure `Dockerfile` appears at the TOP level of the repo on GitHub (not inside a subfolder).
3. On render.com: New -> Blueprint -> pick the repo -> Apply.
   (Or New -> Web Service -> Language: Docker -> Instance: Free -> Health check path: /api/health.)
4. Open the service -> Environment -> set LLM_API_KEY to your NVIDIA key from build.nvidia.com.
   Skip this to run in Offline Mode (still fully works).
5. Wait for the build (about 3-5 min). When it says Live, open your https://<name>.onrender.com URL.

Notes
- Free tier sleeps after 15 min idle; first visit afterwards takes ~1 min. Open it before a demo.
- Every `git push` to main redeploys automatically.
- Build failed? Open the Logs tab and send me the error.
