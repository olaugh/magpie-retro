// ROM configuration - hybrid algorithm ROMs
const ROMS = {
    nwl23: { name: 'NWL23 (North American)', file: 'play/roms/nwl23-hybrid.bin' },
    csw24: { name: 'CSW24 (International)', file: 'play/roms/csw24-hybrid.bin' }
};

// Constants
const TURBO_FAST_FORWARD_RATIO = 100;

// State
let turboEnabled = false;
let fpsInterval = null;

// Get ROM from URL or default
function getSelectedRom() {
    const params = new URLSearchParams(window.location.search);
    const rom = params.get('rom');
    return (rom && ROMS[rom]) ? rom : 'nwl23';
}

document.addEventListener('DOMContentLoaded', function() {
    const romSelect = document.getElementById('rom-select');
    const loadBtn = document.getElementById('load-btn');
    const turboBtn = document.getElementById('turbo-btn');
    const selectedRom = getSelectedRom();

    romSelect.value = selectedRom;

    // Load button
    loadBtn.addEventListener('click', function() {
        const newRom = romSelect.value;
        const currentRom = getSelectedRom();
        if (newRom !== currentRom || !window.EJS_emulator) {
            window.location.href = '?rom=' + newRom;
        }
    });

    // Turbo toggle
    function toggleTurbo() {
        turboEnabled = !turboEnabled;
        turboBtn.textContent = turboEnabled ? 'Turbo: ON' : 'Turbo: OFF';
        turboBtn.classList.toggle('active', turboEnabled);
        if (window.EJS_emulator && window.EJS_emulator.gameManager) {
            if (turboEnabled) {
                window.EJS_emulator.gameManager.setFastForwardRatio(TURBO_FAST_FORWARD_RATIO);
                window.EJS_emulator.gameManager.toggleFastForward(1);
            } else {
                window.EJS_emulator.gameManager.setFastForwardRatio(1);
                window.EJS_emulator.gameManager.toggleFastForward(0);
            }
        }
    }

    turboBtn.addEventListener('click', toggleTurbo);

    document.addEventListener('keydown', function(e) {
        if (e.code === 'Space' && e.target === document.body) {
            e.preventDefault();
            toggleTurbo();
        }
    });

    // Load emulator
    loadEmulator(selectedRom);

    // FPS counter
    const fpsDisplay = document.getElementById('fps-display');
    let lastFrameNum = 0;
    let lastTime = performance.now();

    fpsInterval = setInterval(function() {
        if (window.EJS_emulator && window.EJS_emulator.gameManager &&
            typeof window.EJS_emulator.gameManager.getFrameNum === 'function') {
            try {
                const frameNum = window.EJS_emulator.gameManager.getFrameNum();
                const now = performance.now();
                const elapsed = (now - lastTime) / 1000;
                if (elapsed >= 0.5 && lastFrameNum > 0) {
                    const fps = (frameNum - lastFrameNum) / elapsed;
                    fpsDisplay.textContent = 'FPS ' + Math.round(fps);
                }
                lastFrameNum = frameNum;
                lastTime = now;
            } catch (e) {}
        }
    }, 500);
});

function loadEmulator(romKey) {
    const rom = ROMS[romKey];
    if (!rom) return;

    // Resolve ROM path relative to current page URL for GitHub Pages compatibility
    const romUrl = new URL(rom.file, window.location.href).href;

    window.EJS_player = '#game';
    window.EJS_core = 'genesis_plus_gx';
    window.EJS_gameUrl = romUrl;
    window.EJS_gameName = rom.name;
    window.EJS_color = '#22C55E';
    window.EJS_startOnLoaded = true;
    window.EJS_pathtodata = 'https://cdn.emulatorjs.org/stable/data/';

    const script = document.createElement('script');
    script.src = 'https://cdn.emulatorjs.org/stable/data/loader.js';
    script.integrity = 'sha384-CwARP2ej7UlPGk5E0IPt89lxjdb3t7zStyLR6PL7Sg4xzHSrvXh/R4vbb4PrSv6U';
    script.crossOrigin = 'anonymous';
    document.body.appendChild(script);
}
