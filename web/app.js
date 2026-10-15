/* ============================================================
   QuizForge — app.js
   Vanilla JS front-end: input, analysis call, insights, quiz,
   flashcards, results, confetti, export, theme.
   Security note: user/AI text is only ever written with
   textContent / createTextNode, never innerHTML.
   ============================================================ */
(function () {
    'use strict';

    /* ── Sample notes (~300 words) ─────────────────────────────── */
    const SAMPLE_NOTES = `Photosynthesis is the process by which green plants, algae and some bacteria convert light energy into chemical energy stored in glucose. It takes place mainly in the chloroplasts of leaf cells, which contain the green pigment chlorophyll. Chlorophyll absorbs red and blue light and reflects green light, which is why most leaves look green.

The process has two major stages. The light-dependent reactions occur in the thylakoid membranes of the chloroplast. During these reactions, light energy splits water molecules, releasing oxygen as a by-product and producing the energy carriers ATP and NADPH. About 1% of the energy in sunlight is captured by a typical plant leaf.

The Calvin cycle is called the light-independent stage because it does not need light directly. It occurs in the stroma, the fluid-filled space around the thylakoids. In the Calvin cycle, the enzyme RuBisCO fixes carbon dioxide into a three-carbon molecule, and ATP and NADPH are then used to build glucose. Carbon fixation refers to the process of converting inorganic carbon dioxide into organic compounds.

The overall equation for photosynthesis is 6CO2 + 6H2O + light energy gives C6H12O6 + 6O2. Several factors affect the rate of photosynthesis, including light intensity, temperature and carbon dioxide concentration. Above about 40 degrees Celsius, enzymes begin to denature and the rate drops sharply.

Some plants have evolved special adaptations. C4 plants such as maize and sugarcane concentrate carbon dioxide in bundle sheath cells, which reduces photorespiration in hot climates. CAM plants such as cacti open their stomata only at night to save water. Photorespiration is a wasteful process in which RuBisCO binds oxygen instead of carbon dioxide.

Photosynthesis sustains nearly all life on Earth because it produces the oxygen we breathe and forms the base of most food chains. It also removes carbon dioxide from the atmosphere, which makes forests and oceans important carbon sinks in the fight against climate change.`;

    const STATUS_MESSAGES = [
        'Cleaning notes...',
        'Ranking sentences...',
        'Finding key terms...',
        'Writing questions...'
    ];

    const REDUCED_MOTION = window.matchMedia &&
        window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    /* ── Tiny DOM helpers ──────────────────────────────────────── */
    const $ = (id) => document.getElementById(id);

    /** Create an element with optional class and text (safe: textContent). */
    function el(tag, cls, text) {
        const node = document.createElement(tag);
        if (cls) node.className = cls;
        if (text !== undefined && text !== null) node.textContent = String(text);
        return node;
    }

    function show(node) { if (node) node.hidden = false; }
    function hide(node) { if (node) node.hidden = true; }

    function clearNode(node) {
        while (node.firstChild) node.removeChild(node.firstChild);
    }

    function countWords(text) {
        const m = text.trim().match(/\S+/g);
        return m ? m.length : 0;
    }

    function shuffle(arr) {
        const a = arr.slice();
        for (let i = a.length - 1; i > 0; i--) {
            const j = Math.floor(Math.random() * (i + 1));
            [a[i], a[j]] = [a[j], a[i]];
        }
        return a;
    }

    function safeStorage(action, key, value) {
        try {
            if (action === 'get') return window.localStorage.getItem(key);
            window.localStorage.setItem(key, value);
        } catch (e) { /* storage unavailable (private mode) */ }
        return null;
    }

    /* ── Element references ────────────────────────────────────── */
    const notesInput     = $('notesInput');
    const wordCounter    = $('wordCounter');
    const sampleBtn      = $('sampleBtn');
    const dropZone       = $('dropZone');
    const dropOverlay    = $('dropOverlay');
    const numQuestions   = $('numQuestions');
    const numQuestionsV  = $('numQuestionsValue');
    const generateBtn    = $('generateBtn');
    const statusText     = $('statusText');
    const modeBadge      = $('modeBadge');
    const themeToggle    = $('themeToggle');

    const inputSection   = $('inputSection');
    const insightsSec    = $('insightsSection');
    const quizSection    = $('quizSection');
    const resultsSection = $('resultsSection');
    const fcSection      = $('flashcardsSection');
    const exportSection  = $('exportSection');

    /* ── App state ─────────────────────────────────────────────── */
    const state = {
        difficulty: 'medium',
        mode: 'both',
        data: null,            // last /api/analyze response
        view: 'quiz',          // 'quiz' | 'flashcards'
        quiz: null,
        fc: null,
        loading: false
    };

    /* ============================================================
       Toasts
       ============================================================ */
    function toast(message, type) {
        const box = $('toastContainer');
        const t = el('div', 'toast toast--' + (type || 'error'), message);
        box.appendChild(t);
        setTimeout(() => { if (t.parentNode) t.parentNode.removeChild(t); }, 4000);
    }

    /* ============================================================
       Theme (saved in localStorage)
       ============================================================ */
    function applyTheme(theme) {
        document.documentElement.setAttribute('data-theme', theme);
        const icon = themeToggle.querySelector('.theme-toggle__icon');
        if (icon) icon.textContent = theme === 'dark' ? '🌙' : '☀️';
    }

    function initTheme() {
        let theme = safeStorage('get', 'quizforge-theme');
        if (theme !== 'light' && theme !== 'dark') {
            theme = (window.matchMedia &&
                window.matchMedia('(prefers-color-scheme: light)').matches) ? 'light' : 'dark';
        }
        applyTheme(theme);
        themeToggle.addEventListener('click', () => {
            const next = document.documentElement.getAttribute('data-theme') === 'dark'
                ? 'light' : 'dark';
            applyTheme(next);
            safeStorage('set', 'quizforge-theme', next);
        });
    }

    /* ============================================================
       Input card: counter, sample, drag & drop, controls
       ============================================================ */
    function updateWordCounter() {
        const n = countWords(notesInput.value);
        wordCounter.textContent = n + (n === 1 ? ' word' : ' words');
    }

    function loadTextFile(file) {
        if (!file) return;
        const isText = file.type === 'text/plain' || /\.txt$/i.test(file.name);
        if (!isText) { toast('Please drop a .txt file.'); return; }
        if (file.size > 100 * 1024) { toast('File is too large (max 100 KB).'); return; }
        const reader = new FileReader();
        reader.onload = () => {
            notesInput.value = String(reader.result || '').slice(0, 102400);
            updateWordCounter();
            toast('Loaded ' + file.name, 'success');
        };
        reader.onerror = () => toast('Could not read that file.');
        reader.readAsText(file);
    }

    function initInput() {
        notesInput.addEventListener('input', updateWordCounter);
        sampleBtn.addEventListener('click', () => {
            notesInput.value = SAMPLE_NOTES;
            updateWordCounter();
            notesInput.focus();
        });

        numQuestions.addEventListener('input', () => {
            numQuestionsV.textContent = numQuestions.value;
        });

        // Drag & drop of .txt files
        let dragDepth = 0;
        ['dragenter', 'dragover'].forEach((ev) => {
            dropZone.addEventListener(ev, (e) => {
                e.preventDefault();
                if (ev === 'dragenter') dragDepth++;
                dropOverlay.classList.add('drop-overlay--active');
            });
        });
        dropZone.addEventListener('dragleave', (e) => {
            e.preventDefault();
            dragDepth = Math.max(0, dragDepth - 1);
            if (dragDepth === 0) dropOverlay.classList.remove('drop-overlay--active');
        });
        dropZone.addEventListener('drop', (e) => {
            e.preventDefault();
            dragDepth = 0;
            dropOverlay.classList.remove('drop-overlay--active');
            const files = e.dataTransfer && e.dataTransfer.files;
            if (files && files.length) loadTextFile(files[0]);
        });
        // Stop the browser from opening files dropped elsewhere on the page
        window.addEventListener('dragover', (e) => e.preventDefault());
        window.addEventListener('drop', (e) => e.preventDefault());

        // Segmented controls (difficulty + mode)
        document.querySelectorAll('.segmented').forEach((group) => {
            const buttons = Array.from(group.querySelectorAll('.segmented__btn[data-value]'));
            if (!buttons.length || group.hasAttribute('data-tabs')) return;
            const select = (btn) => {
                buttons.forEach((b) => {
                    const on = b === btn;
                    b.classList.toggle('segmented__btn--active', on);
                    b.setAttribute('aria-checked', on ? 'true' : 'false');
                    b.tabIndex = on ? 0 : -1;
                });
                const labelId = group.getAttribute('aria-labelledby');
                if (labelId === 'difficultyLabel') state.difficulty = btn.dataset.value;
                if (labelId === 'modeLabel') state.mode = btn.dataset.value;
            };
            buttons.forEach((btn, i) => {
                btn.type = 'button';
                btn.addEventListener('click', () => select(btn));
                btn.addEventListener('keydown', (e) => {
                    let n = -1;
                    if (e.key === 'ArrowRight' || e.key === 'ArrowDown') n = (i + 1) % buttons.length;
                    if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') n = (i - 1 + buttons.length) % buttons.length;
                    if (n >= 0) { e.preventDefault(); select(buttons[n]); buttons[n].focus(); }
                });
            });
            const active = buttons.find((b) => b.classList.contains('segmented__btn--active')) || buttons[0];
            select(active);
        });
    }

    /* ============================================================
       Loading state: spinner, rotating status, skeleton
       ============================================================ */
    let statusTimer = null;
    let skeletonBox = null;

    function setLoading(on) {
        state.loading = on;
        generateBtn.disabled = on;
        generateBtn.setAttribute('aria-busy', on ? 'true' : 'false');
        generateBtn.querySelector('.btn__text').hidden = on;
        generateBtn.querySelector('.btn__loader').hidden = !on;

        if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
        if (on) {
            let i = 0;
            statusText.textContent = STATUS_MESSAGES[0];
            statusTimer = setInterval(() => {
                i = (i + 1) % STATUS_MESSAGES.length;
                statusText.textContent = STATUS_MESSAGES[i];
            }, 1100);

            skeletonBox = el('div', 'skeleton-box');
            skeletonBox.setAttribute('aria-hidden', 'true');
            skeletonBox.appendChild(el('div', 'skeleton skeleton--tall'));
            skeletonBox.appendChild(el('div', 'skeleton skeleton--row'));
            skeletonBox.appendChild(el('div', 'skeleton skeleton--row'));
            inputSection.insertAdjacentElement('afterend', skeletonBox);
        } else if (skeletonBox) {
            skeletonBox.remove();
            skeletonBox = null;
        }
    }

    /* ============================================================
       API call
       ============================================================ */
    async function analyze() {
        const text = notesInput.value.trim();
        const words = countWords(text);
        if (words < 30) {
            toast('Please add at least 30 words of notes for good questions.');
            notesInput.focus();
            return;
        }
        if (new Blob([text]).size > 100 * 1024) {
            toast('Notes are too long (max 100 KB).');
            return;
        }

        setLoading(true);
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 60000);

        try {
            const res = await fetch('/api/analyze', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    text: text,
                    num_questions: parseInt(numQuestions.value, 10) || 8,
                    difficulty: state.difficulty,
                    mode: state.mode
                }),
                signal: controller.signal
            });

            let payload = null;
            try { payload = await res.json(); } catch (e) { /* non-JSON error */ }

            if (!res.ok || !payload || payload.error) {
                throw new Error((payload && payload.error) || ('Server error (' + res.status + ')'));
            }
            handleResult(payload);
        } catch (err) {
            if (err.name === 'AbortError') {
                toast('The request timed out. Try shorter notes or try again.');
            } else {
                toast(err.message || 'Something went wrong.');
            }
        } finally {
            clearTimeout(timeout);
            setLoading(false);
        }
    }

    /* ============================================================
       Result handling
       ============================================================ */
    function handleResult(data) {
        state.data = data;
        data.mcqs = Array.isArray(data.mcqs) ? data.mcqs : [];
        data.flashcards = Array.isArray(data.flashcards) ? data.flashcards : [];

        const wantMcq = state.mode !== 'flashcards';
        const wantFc  = state.mode !== 'mcq';
        const hasMcq  = wantMcq && data.mcqs.length > 0;
        const hasFc   = wantFc && data.flashcards.length > 0;

        if (!hasMcq && !hasFc) {
            toast('No questions could be generated. Try longer or richer notes.');
            return;
        }

        renderModeBadge(data.source);
        renderInsights(data);
        show(insightsSec);
        show(exportSection);
        buildViewTabs(hasMcq, hasFc);

        if (hasMcq) startQuiz(data.mcqs);
        if (hasFc) initFlashcards(data.flashcards);
        switchView(hasMcq ? 'quiz' : 'flashcards');

        insightsSec.scrollIntoView({ behavior: REDUCED_MOTION ? 'auto' : 'smooth', block: 'start' });
        toast('Generated ' + (hasMcq ? data.mcqs.length + ' questions' : '') +
              (hasMcq && hasFc ? ' and ' : '') +
              (hasFc ? data.flashcards.length + ' flashcards' : ''), 'success');
    }

    function renderModeBadge(source) {
        const ai = source === 'ai';
        modeBadge.classList.remove('badge--ai', 'badge--offline');
        modeBadge.classList.add(ai ? 'badge--ai' : 'badge--offline');
        modeBadge.querySelector('.badge__text').textContent = ai ? 'AI Mode' : 'Offline Mode';
        show(modeBadge);
    }

    /* ── Insights: stats, chips, heatmap ──────────────────────── */
    function countUp(node, target, suffix) {
        const end = Math.round(Number(target) || 0);
        if (REDUCED_MOTION) { node.textContent = end + (suffix || ''); return; }
        const duration = 900;
        const t0 = performance.now();
        function frame(now) {
            const p = Math.min(1, (now - t0) / duration);
            const eased = 1 - Math.pow(1 - p, 3);
            node.textContent = Math.round(end * eased) + (suffix || '');
            if (p < 1) requestAnimationFrame(frame);
        }
        requestAnimationFrame(frame);
    }

    function renderInsights(data) {
        const s = data.stats || {};
        countUp($('statWords'), s.words);
        countUp($('statSentences'), s.sentences);
        countUp($('statChunks'), s.chunks);
        countUp($('statReduction'), s.reduction_percent);

        // Key term chips: size and opacity follow the weight
        const chips = $('keyTermsChips');
        clearNode(chips);
        (data.key_terms || []).forEach((kt) => {
            const w = Math.max(0, Math.min(1, Number(kt.weight) || 0));
            const chip = el('span', 'chip' + (w > 0.7 ? ' chip--strong' : ''), kt.term);
            chip.setAttribute('role', 'listitem');
            chip.style.fontSize = (0.8 + w * 0.5).toFixed(2) + 'rem';
            chip.style.opacity = (0.55 + w * 0.45).toFixed(2);
            chip.title = 'Importance: ' + Math.round(w * 100) + '%';
            chips.appendChild(chip);
        });

        // Heatmap: faint to strong highlight by sentence score
        const heat = $('heatmapContent');
        clearNode(heat);
        (data.sentences || []).forEach((sent) => {
            const sc = Math.max(0, Math.min(1, Number(sent.score) || 0));
            const span = el('span', 'heatmap__sentence');
            span.setAttribute('role', 'listitem');
            span.tabIndex = 0;
            span.style.background = 'rgba(124, 58, 237, ' + (0.06 + sc * 0.5).toFixed(2) + ')';
            span.appendChild(document.createTextNode(sent.text + ' '));
            span.appendChild(el('span', 'heatmap__tooltip', 'Importance: ' + Math.round(sc * 100) + '%'));
            heat.appendChild(span);
        });
    }

    /* ── View tabs (Quiz / Flashcards) ────────────────────────── */
    let viewTabs = null;

    function buildViewTabs(hasMcq, hasFc) {
        if (viewTabs) { viewTabs.remove(); viewTabs = null; }
        if (!(hasMcq && hasFc)) return; // single mode: no tabs needed

        viewTabs = el('div', 'view-tabs');
        const group = el('div', 'segmented');
        group.setAttribute('data-tabs', '');
        group.setAttribute('role', 'tablist');
        group.setAttribute('aria-label', 'Study mode');
        [['quiz', '🧠 Quiz'], ['flashcards', '🃏 Flashcards']].forEach((pair) => {
            const b = el('button', 'segmented__btn', pair[1]);
            b.type = 'button';
            b.dataset.view = pair[0];
            b.setAttribute('role', 'tab');
            b.addEventListener('click', () => switchView(pair[0]));
            group.appendChild(b);
        });
        viewTabs.appendChild(group);
        quizSection.insertAdjacentElement('beforebegin', viewTabs);
    }

    function switchView(view) {
        state.view = view;
        hide(quizSection); hide(resultsSection); hide(fcSection);
        if (view === 'quiz') {
            if (state.quiz && state.quiz.finished) show(resultsSection);
            else show(quizSection);
        } else {
            show(fcSection);
        }
        if (viewTabs) {
            viewTabs.querySelectorAll('.segmented__btn').forEach((b) => {
                const on = b.dataset.view === view;
                b.classList.toggle('segmented__btn--active', on);
                b.setAttribute('aria-selected', on ? 'true' : 'false');
            });
        }
    }

    /* ============================================================
       Quiz
       ============================================================ */
    /** Validate and normalise MCQs coming from the server. */
    function cleanMcqs(list) {
        return list.filter((q) =>
            q && typeof q.question === 'string' && Array.isArray(q.options) &&
            q.options.length >= 2 && Number.isInteger(q.answer_index) &&
            q.answer_index >= 0 && q.answer_index < q.options.length
        ).map((q) => ({
            question: q.question,
            options: q.options.map(String),
            answer_index: q.answer_index,
            explanation: typeof q.explanation === 'string' ? q.explanation : ''
        }));
    }

    function startQuiz(list) {
        const questions = cleanMcqs(list);
        state.quiz = {
            all: questions,
            questions: questions,
            index: 0,
            score: 0,
            streak: 0,
            answered: false,
            missed: [],
            order: [],
            correctPos: 0,
            finished: false
        };
        hide(resultsSection);
        renderQuestion();
    }

    function renderQuestion() {
        const qz = state.quiz;
        const q = qz.questions[qz.index];
        const total = qz.questions.length;
        qz.answered = false;

        // Shuffle display order so the correct answer is not always in the same slot
        qz.order = shuffle(q.options.map((_, i) => i));
        qz.correctPos = qz.order.indexOf(q.answer_index);

        $('questionCounter').textContent = 'Question ' + (qz.index + 1) + ' / ' + total;
        $('progressFill').style.width = ((qz.index / total) * 100) + '%';
        $('quizScore').textContent = qz.score;
        $('quizStreak').textContent = qz.streak;
        $('questionNumber').textContent = 'Q' + (qz.index + 1);
        $('questionText').textContent = q.question;

        const list = $('optionsList');
        clearNode(list);
        qz.order.forEach((origIdx, pos) => {
            const btn = el('button', 'option-btn');
            btn.type = 'button';
            btn.setAttribute('role', 'radio');
            btn.setAttribute('aria-checked', 'false');
            btn.dataset.pos = String(pos);
            btn.appendChild(el('span', 'option-btn__key', pos + 1));
            btn.appendChild(el('span', 'option-btn__text', q.options[origIdx]));
            btn.addEventListener('click', () => answerQuestion(pos));
            list.appendChild(btn);
        });

        hide($('explanationBox'));
        hide($('nextBtn'));
        $('nextBtn').textContent = qz.index === total - 1 ? 'See Results →' : 'Next Question →';

        const qt = $('questionText');
        qt.tabIndex = -1;
        qt.focus({ preventScroll: true });
    }

    function answerQuestion(pos) {
        const qz = state.quiz;
        if (!qz || qz.answered || qz.finished) return;
        qz.answered = true;

        const q = qz.questions[qz.index];
        const buttons = $('optionsList').querySelectorAll('.option-btn');
        const isCorrect = pos === qz.correctPos;

        buttons.forEach((b, i) => {
            b.disabled = true;
            if (i === qz.correctPos) b.classList.add('option-btn--correct');
        });
        buttons[pos].setAttribute('aria-checked', 'true');

        if (isCorrect) {
            qz.score++;
            qz.streak++;
        } else {
            buttons[pos].classList.add('option-btn--wrong');
            qz.streak = 0;
            qz.missed.push({
                question: q.question,
                correct: q.options[q.answer_index],
                explanation: q.explanation,
                raw: q
            });
        }

        $('quizScore').textContent = qz.score;
        $('quizStreak').textContent = qz.streak;
        $('progressFill').style.width = (((qz.index + 1) / qz.questions.length) * 100) + '%';

        const expl = $('explanationText');
        clearNode(expl);
        expl.appendChild(el('strong', null, isCorrect ? '✅ Correct! ' : '❌ Not quite. '));
        if (!isCorrect) {
            expl.appendChild(document.createTextNode('Answer: ' + q.options[q.answer_index] + '. '));
        }
        if (q.explanation) expl.appendChild(document.createTextNode(q.explanation));
        show($('explanationBox'));
        show($('nextBtn'));
        $('nextBtn').focus({ preventScroll: true });
    }

    function nextQuestion() {
        const qz = state.quiz;
        if (!qz || !qz.answered) return;
        if (qz.index < qz.questions.length - 1) {
            qz.index++;
            renderQuestion();
        } else {
            finishQuiz();
        }
    }

    function finishQuiz() {
        const qz = state.quiz;
        qz.finished = true;
        const total = qz.questions.length;
        const pct = total ? Math.round((qz.score / total) * 100) : 0;

        hide(quizSection);
        show(resultsSection);

        // Animated score ring
        const ring = $('scoreRingFill');
        const circumference = 326.7;
        ring.style.strokeDashoffset = String(circumference);
        requestAnimationFrame(() => requestAnimationFrame(() => {
            ring.style.strokeDashoffset = String(circumference * (1 - pct / 100));
        }));
        countUp($('scoreRingLabel'), pct, '%');

        let msg;
        if (pct === 100) msg = 'Perfect score! 🏆 You nailed it.';
        else if (pct >= 70) msg = 'Great job! 🎉 You know this material well.';
        else if (pct >= 40) msg = 'Good effort! 💪 Review the missed ones and try again.';
        else msg = 'Keep going! 📚 Re-read the notes and retry.';
        $('resultsMessage').textContent = qz.score + ' / ' + total + ' correct — ' + msg;

        // Missed questions
        const missedList = $('missedList');
        clearNode(missedList);
        if (qz.missed.length) {
            qz.missed.forEach((m, i) => {
                const item = el('div', 'missed-item');
                item.appendChild(el('div', 'missed-item__q', (i + 1) + '. ' + m.question));
                item.appendChild(el('div', 'missed-item__correct', '✔ Correct answer: ' + m.correct));
                if (m.explanation) item.appendChild(el('div', 'missed-item__explanation', m.explanation));
                missedList.appendChild(item);
            });
            show($('missedCard'));
            show($('retryMissedBtn'));
        } else {
            hide($('missedCard'));
            hide($('retryMissedBtn'));
        }

        if (pct >= 70) launchConfetti();
        resultsSection.scrollIntoView({ behavior: REDUCED_MOTION ? 'auto' : 'smooth', block: 'start' });
    }

    function retryQuiz(missedOnly) {
        const qz = state.quiz;
        if (!qz) return;
        const list = missedOnly ? qz.missed.map((m) => m.raw) : qz.all;
        if (!list.length) return;
        const all = qz.all;
        state.quiz = {
            all: all, questions: list, index: 0, score: 0, streak: 0,
            answered: false, missed: [], order: [], correctPos: 0, finished: false
        };
        hide(resultsSection);
        show(quizSection);
        renderQuestion();
        quizSection.scrollIntoView({ behavior: REDUCED_MOTION ? 'auto' : 'smooth', block: 'start' });
    }

    function resetToNewNotes() {
        state.quiz = null;
        state.fc = null;
        state.data = null;
        hide(insightsSec); hide(quizSection); hide(resultsSection);
        hide(fcSection); hide(exportSection); hide(modeBadge);
        if (viewTabs) { viewTabs.remove(); viewTabs = null; }
        inputSection.scrollIntoView({ behavior: REDUCED_MOTION ? 'auto' : 'smooth', block: 'start' });
        notesInput.focus({ preventScroll: true });
    }

    /* ============================================================
       Flashcards
       ============================================================ */
    function initFlashcards(list) {
        const cards = list.filter((c) => c && typeof c.front === 'string' && typeof c.back === 'string');
        state.fc = {
            cards: cards,
            queue: cards.map((_, i) => i),
            pos: 0,
            mastered: 0,
            flipped: false
        };
        renderFlashcard(false);
    }

    function setFlipped(flipped) {
        const fc = state.fc;
        if (!fc) return;
        fc.flipped = flipped;
        $('fcCard').classList.toggle('fc-card--flipped', flipped);
        $('fcCard').setAttribute('aria-label', flipped ? 'Showing answer. Press to flip back' : 'Press to reveal answer');
    }

    function renderFlashcard(animate) {
        const fc = state.fc;
        if (!fc) return;
        const total = fc.cards.length;
        $('fcMastered').textContent = fc.mastered + ' / ' + total;

        const apply = () => {
            const card = $('fcCard');
            if (fc.queue.length === 0) {
                card.classList.add('fc-card--done');
                $('fcFront').textContent = '🎉 All cards mastered!';
                $('fcBack').textContent = 'Press "Review Again" to restart the deck.';
                $('fcCounter').textContent = 'Deck complete';
                return;
            }
            card.classList.remove('fc-card--done');
            if (fc.pos >= fc.queue.length) fc.pos = 0;
            if (fc.pos < 0) fc.pos = fc.queue.length - 1;
            const c = fc.cards[fc.queue[fc.pos]];
            $('fcFront').textContent = c.front;
            $('fcBack').textContent = c.back;
            $('fcCounter').textContent = 'Card ' + (fc.pos + 1) + ' / ' + fc.queue.length;
        };

        // If the card is showing its back, flip first so the answer never flashes
        if (animate && fc.flipped && !REDUCED_MOTION) {
            setFlipped(false);
            setTimeout(apply, 200);
        } else {
            setFlipped(false);
            apply();
        }
    }

    function fcMove(delta) {
        const fc = state.fc;
        if (!fc || fc.queue.length === 0) return;
        fc.pos = (fc.pos + delta + fc.queue.length) % fc.queue.length;
        renderFlashcard(true);
    }

    function fcGotIt() {
        const fc = state.fc;
        if (!fc || fc.queue.length === 0) return;
        fc.queue.splice(fc.pos, 1);
        fc.mastered++;
        if (fc.pos >= fc.queue.length) fc.pos = 0;
        renderFlashcard(true);
        if (fc.queue.length === 0) {
            toast('All flashcards mastered! 🎉', 'success');
            launchConfetti();
        }
    }

    function fcReview() {
        const fc = state.fc;
        if (!fc) return;
        if (fc.queue.length === 0) {           // restart the whole deck
            fc.queue = fc.cards.map((_, i) => i);
            fc.pos = 0;
            fc.mastered = 0;
        } else {
            fc.pos = (fc.pos + 1) % fc.queue.length; // keep it in the deck, move on
        }
        renderFlashcard(true);
    }

    function initFlashcardEvents() {
        const card = $('fcCard');
        card.addEventListener('click', () => {
            if (state.fc && state.fc.queue.length) setFlipped(!state.fc.flipped);
        });
        card.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                if (state.fc && state.fc.queue.length) setFlipped(!state.fc.flipped);
            }
        });
        $('fcPrevBtn').addEventListener('click', () => fcMove(-1));
        $('fcNextBtn').addEventListener('click', () => fcMove(1));
        $('fcGotItBtn').addEventListener('click', fcGotIt);
        $('fcReviewBtn').addEventListener('click', fcReview);

        // Swipe support
        let startX = null;
        const cont = $('fcContainer');
        cont.addEventListener('touchstart', (e) => {
            startX = e.changedTouches[0].clientX;
        }, { passive: true });
        cont.addEventListener('touchend', (e) => {
            if (startX === null) return;
            const dx = e.changedTouches[0].clientX - startX;
            startX = null;
            if (Math.abs(dx) > 50) fcMove(dx < 0 ? 1 : -1);
        }, { passive: true });
    }

    /* ============================================================
       Keyboard shortcuts
       ============================================================ */
    function initKeyboard() {
        document.addEventListener('keydown', (e) => {
            if (e.ctrlKey || e.metaKey || e.altKey) return;
            const tag = (e.target && e.target.tagName) || '';
            if (tag === 'TEXTAREA' || tag === 'INPUT') return;

            const quizVisible = !quizSection.hidden;
            const fcVisible = !fcSection.hidden;

            if (quizVisible && state.quiz && !state.quiz.finished) {
                if (!state.quiz.answered && /^[1-9]$/.test(e.key)) {
                    const pos = parseInt(e.key, 10) - 1;
                    if (pos < state.quiz.order.length) answerQuestion(pos);
                } else if (e.key === 'Enter' && state.quiz.answered && tag !== 'BUTTON') {
                    e.preventDefault();
                    nextQuestion();
                }
            } else if (fcVisible && state.fc) {
                if (e.key === 'ArrowRight') { e.preventDefault(); fcMove(1); }
                else if (e.key === 'ArrowLeft') { e.preventDefault(); fcMove(-1); }
            }
        });
    }

    /* ============================================================
       Confetti (canvas, no libraries)
       ============================================================ */
    let confettiRunning = false;

    function launchConfetti() {
        if (REDUCED_MOTION || confettiRunning) return;
        const canvas = $('confettiCanvas');
        const ctx = canvas.getContext('2d');
        if (!ctx) return;

        const dpr = window.devicePixelRatio || 1;
        const W = window.innerWidth, H = window.innerHeight;
        canvas.width = W * dpr;
        canvas.height = H * dpr;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

        const colors = ['#7c3aed', '#06b6d4', '#22c55e', '#f59e0b', '#ef4444', '#ec4899'];
        const pieces = [];
        for (let i = 0; i < 160; i++) {
            pieces.push({
                x: W / 2 + (Math.random() - 0.5) * 120,
                y: H * 0.35,
                vx: (Math.random() - 0.5) * 14,
                vy: -Math.random() * 12 - 4,
                size: 5 + Math.random() * 6,
                rot: Math.random() * Math.PI,
                vr: (Math.random() - 0.5) * 0.3,
                color: colors[i % colors.length]
            });
        }

        confettiRunning = true;
        const t0 = performance.now();
        const DURATION = 3200;

        function frame(now) {
            const elapsed = now - t0;
            ctx.clearRect(0, 0, W, H);
            const fade = Math.max(0, 1 - Math.max(0, elapsed - 2200) / 1000);
            ctx.globalAlpha = fade;
            pieces.forEach((p) => {
                p.vy += 0.35;          // gravity
                p.vx *= 0.99;          // air drag
                p.x += p.vx;
                p.y += p.vy;
                p.rot += p.vr;
                ctx.save();
                ctx.translate(p.x, p.y);
                ctx.rotate(p.rot);
                ctx.fillStyle = p.color;
                ctx.fillRect(-p.size / 2, -p.size / 2, p.size, p.size * 0.6);
                ctx.restore();
            });
            if (elapsed < DURATION) {
                requestAnimationFrame(frame);
            } else {
                ctx.clearRect(0, 0, W, H);
                confettiRunning = false;
            }
        }
        requestAnimationFrame(frame);
    }

    /* ============================================================
       Export: JSON download + print sheet
       ============================================================ */
    function downloadJson() {
        if (!state.data) return;
        const d = state.data;
        const out = {
            generated_at: new Date().toISOString(),
            source: d.source,
            stats: d.stats,
            key_terms: d.key_terms,
            mcqs: d.mcqs,
            flashcards: d.flashcards
        };
        const blob = new Blob([JSON.stringify(out, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'quizforge-quiz.json';
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    }

    function buildPrintSheet() {
        const old = document.querySelector('.print-sheet');
        if (old) old.remove();
        const d = state.data;
        const sheet = el('div', 'print-sheet');
        sheet.appendChild(el('h1', null, 'QuizForge — Study Sheet'));

        if (d.key_terms && d.key_terms.length) {
            sheet.appendChild(el('h2', null, 'Key Terms'));
            sheet.appendChild(el('p', null, d.key_terms.map((k) => k.term).join(', ')));
        }
        if (d.mcqs.length) {
            sheet.appendChild(el('h2', null, 'Multiple Choice Questions'));
            const ol = el('ol');
            d.mcqs.forEach((q) => {
                const li = el('li');
                li.appendChild(el('div', null, q.question));
                const letters = ['A', 'B', 'C', 'D', 'E', 'F'];
                (q.options || []).forEach((o, i) => {
                    const line = el('div', i === q.answer_index ? 'answer' : null,
                        letters[i] + ') ' + o + (i === q.answer_index ? '  ✔' : ''));
                    li.appendChild(line);
                });
                if (q.explanation) li.appendChild(el('div', null, 'Why: ' + q.explanation));
                ol.appendChild(li);
            });
            sheet.appendChild(ol);
        }
        if (d.flashcards.length) {
            sheet.appendChild(el('h2', null, 'Flashcards'));
            const ol = el('ol');
            d.flashcards.forEach((c) => {
                const li = el('li');
                li.appendChild(el('div', 'answer', c.front));
                li.appendChild(el('div', null, c.back));
                ol.appendChild(li);
            });
            sheet.appendChild(ol);
        }
        document.querySelector('main').appendChild(sheet);
    }

    function printSheet() {
        if (!state.data) return;
        buildPrintSheet();
        document.body.classList.add('printing');
        const cleanup = () => {
            document.body.classList.remove('printing');
            const s = document.querySelector('.print-sheet');
            if (s) s.remove();
            window.removeEventListener('afterprint', cleanup);
        };
        window.addEventListener('afterprint', cleanup);
        window.print();
    }

    /* ============================================================
       Boot
       ============================================================ */
    function init() {
        initTheme();
        initInput();
        initFlashcardEvents();
        initKeyboard();
        updateWordCounter();

        generateBtn.addEventListener('click', analyze);
        $('nextBtn').addEventListener('click', nextQuestion);
        $('retryBtn').addEventListener('click', () => retryQuiz(false));
        $('retryMissedBtn').addEventListener('click', () => retryQuiz(true));
        $('newNotesBtn').addEventListener('click', resetToNewNotes);
        $('downloadJsonBtn').addEventListener('click', downloadJson);
        $('printBtn').addEventListener('click', printSheet);

        window.addEventListener('resize', () => {
            if (!confettiRunning) {
                const c = $('confettiCanvas');
                c.width = 0; c.height = 0;
            }
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
