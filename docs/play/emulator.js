// ROM file configuration
const ROMS = {
    nwl: {
        name: 'Shadow NWL',
        file: 'roms/shadow-nwl.bin'
    },
    csw: {
        name: 'Shadow CSW',
        file: 'roms/shadow-csw.bin'
    }
};

// Get ROM selection from URL or default to 'nwl'
function getSelectedRom() {
    const params = new URLSearchParams(window.location.search);
    const rom = params.get('rom');
    return (rom && ROMS[rom]) ? rom : 'nwl';
}

// Turbo mode state
let turboEnabled = false;
let fpsInterval = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    const romSelect = document.getElementById('rom-select');
    const loadBtn = document.getElementById('load-btn');
    const turboBtn = document.getElementById('turbo-btn');
    const selectedRom = getSelectedRom();

    // Set dropdown to match URL parameter
    romSelect.value = selectedRom;

    // Load game button - reloads page with selected ROM
    loadBtn.addEventListener('click', function() {
        const newRom = romSelect.value;
        const currentRom = getSelectedRom();

        if (newRom !== currentRom || !window.EJS_emulator) {
            // Reload page with new ROM parameter
            window.location.href = `?rom=${newRom}`;
        }
    });

    // Turbo toggle function
    function toggleTurbo() {
        turboEnabled = !turboEnabled;
        turboBtn.textContent = turboEnabled ? 'Turbo: ON' : 'Turbo: OFF';
        turboBtn.classList.toggle('active', turboEnabled);

        // Toggle fast-forward in emulator via gameManager
        if (window.EJS_emulator && window.EJS_emulator.gameManager) {
            window.EJS_emulator.gameManager.setFastForwardRatio(0);
            window.EJS_emulator.gameManager.toggleFastForward(turboEnabled ? 1 : 0);
        }
    }

    // Turbo toggle button
    turboBtn.addEventListener('click', toggleTurbo);

    // Space key toggles turbo
    document.addEventListener('keydown', function(e) {
        if (e.code === 'Space' && e.target === document.body) {
            e.preventDefault();
            toggleTurbo();
        }
    });

    // Auto-load the emulator
    loadEmulator(selectedRom);

    // FPS counter using emulator's internal frame count
    const fpsDisplay = document.getElementById('fps-display');
    let lastFrameNum = 0;
    let lastTime = performance.now();

    // Update FPS display every 500ms
    fpsInterval = setInterval(function() {
        // Access emulator's gameManager.getFrameNum() for actual emulated frames
        if (window.EJS_emulator && window.EJS_emulator.gameManager &&
            typeof window.EJS_emulator.gameManager.getFrameNum === 'function') {
            try {
                const frameNum = window.EJS_emulator.gameManager.getFrameNum();
                const now = performance.now();
                const elapsed = (now - lastTime) / 1000;

                if (elapsed >= 0.5 && lastFrameNum > 0) {
                    const fps = (frameNum - lastFrameNum) / elapsed;
                    fpsDisplay.textContent = 'FPS: ' + Math.round(fps);
                }

                lastFrameNum = frameNum;
                lastTime = now;
            } catch (e) {
                // Emulator not ready yet
            }
        }
    }, 500);
});

// Initialize emulator with selected ROM
function loadEmulator(romKey) {
    const rom = ROMS[romKey];
    if (!rom) {
        console.error('Unknown ROM:', romKey);
        return;
    }

    // EmulatorJS configuration
    window.EJS_player = '#game';
    window.EJS_core = 'genesis_plus_gx';
    window.EJS_gameUrl = rom.file;
    window.EJS_gameName = rom.name;
    window.EJS_color = '#6366f1';
    window.EJS_startOnLoaded = true;
    window.EJS_pathtodata = 'https://cdn.emulatorjs.org/stable/data/';

    // Fast-forward: 0 = unlimited speed, or set a multiplier (e.g., 3.0 for 3x)
    window.EJS_fastForwardRatio = 0;

    // Load EmulatorJS
    const script = document.createElement('script');
    script.src = 'https://cdn.emulatorjs.org/stable/data/loader.js';
    document.body.appendChild(script);
}
