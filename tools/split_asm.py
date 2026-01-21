#!/usr/bin/env python3
"""
Split objdump mixed C/assembly output into per-file browsable HTML.

Features:
- Two view modes: Source Order (C primary) and Machine Order (ASM primary)
- Collapsible sections for the secondary content
- Dark theme VS Code-style UI
- File filtering sidebar

Usage:
    python3 split_asm.py input.lst output_dir/
"""

import os
import re
import sys
import html
import json
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class AsmLine:
    """A single assembly instruction."""
    address: int
    hex_bytes: str
    instruction: str
    raw_line: str
    cycles: int = 0  # CPU cycles spent at this address (from profiler)


@dataclass
class SourceBlock:
    """A block of source code with associated assembly."""
    file_path: str
    line_number: int
    source_text: str
    asm_lines: list[AsmLine] = field(default_factory=list)


@dataclass
class AsmBlock:
    """A block of assembly with associated source info."""
    asm_lines: list[AsmLine]
    source_file: Optional[str] = None
    source_line: Optional[int] = None
    source_text: Optional[str] = None


@dataclass
class Function:
    """A function with its source blocks and assembly."""
    name: str
    address: int
    source_blocks: list[SourceBlock] = field(default_factory=list)
    asm_blocks: list[AsmBlock] = field(default_factory=list)


@dataclass
class ProfileData:
    """Profiling data loaded from JSON."""
    sample_rate: int
    total_cycles: int
    address_cycles: dict[int, int]  # address -> cycles


def load_profile(path: str) -> Optional[ProfileData]:
    """Load profile data from JSON file."""
    try:
        with open(path) as f:
            data = json.load(f)
        address_cycles = {
            int(addr, 16): cycles
            for addr, cycles in data.get('addresses', {}).items()
        }
        return ProfileData(
            sample_rate=data.get('sample_rate', 1),
            total_cycles=data.get('total_cycles', 0),
            address_cycles=address_cycles
        )
    except (OSError, json.JSONDecodeError, ValueError) as e:
        print(f"Warning: Failed to load profile {path}: {e}", file=sys.stderr)
        return None


def annotate_with_profile(files: dict[str, list[Function]], profile: ProfileData) -> None:
    """Annotate AsmLines with cycle counts from profile data."""
    for functions in files.values():
        for func in functions:
            for sb in func.source_blocks:
                for asm in sb.asm_lines:
                    asm.cycles = profile.address_cycles.get(asm.address, 0)


def normalize_path(path: str) -> str:
    """Normalize paths from objdump output."""
    prefixes_to_strip = [
        '/proc/self/cwd/',
        '/private/var/tmp/_bazel',
        'bazel-out/',
        'external/',
    ]
    for prefix in prefixes_to_strip:
        if prefix in path:
            idx = path.find(prefix)
            path = path[idx + len(prefix):]
            break
    path = path.lstrip('./')
    for marker in ['/src/', '/inc/', '/build/']:
        if marker in path:
            idx = path.find(marker)
            path = path[idx + 1:]
            break
    return path


def get_display_name(path: str) -> str:
    """Get a short display name for the sidebar."""
    return os.path.basename(normalize_path(path))


def parse_objdump(input_file: str) -> dict[str, list[Function]]:
    """
    Parse objdump -d -S output into structured data.

    Returns dict mapping normalized file paths to list of functions.
    """
    files: dict[str, list[Function]] = defaultdict(list)

    current_func: Optional[Function] = None
    current_file = "_unknown_"
    current_source_block: Optional[SourceBlock] = None
    pending_source_lines: list[tuple[str, int, str]] = []  # (file, line, text)

    # Regex patterns
    func_pattern = re.compile(r'^([0-9a-fA-F]+)\s+<([^>]+)>:')
    file_line_pattern = re.compile(r'^(/[^\s:]+|[a-zA-Z0-9_./-]+\.[chsS]):(\d+)(\s.*)?$')
    asm_pattern = re.compile(r'^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F ]+?)\s+(.+)$')
    section_pattern = re.compile(r'^Disassembly of section ([^:]+):')
    # Filter out discriminator annotations (DWARF debug info for loop iterations)
    discriminator_pattern = re.compile(r'^\s*\(discriminator\s+\d+\)\s*$')

    with open(input_file, 'r', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')

            # Skip empty lines
            if not line.strip():
                continue

            # Skip discriminator annotations (DWARF debug info)
            if discriminator_pattern.match(line):
                continue

            # Section header
            if section_pattern.match(line):
                continue

            # Function header
            func_match = func_pattern.match(line)
            if func_match:
                # Save previous function
                if current_func:
                    files[current_file].append(current_func)

                addr = int(func_match.group(1), 16)
                name = func_match.group(2)
                current_func = Function(name=name, address=addr)
                current_source_block = None
                pending_source_lines = []
                continue

            # Source file:line reference
            file_match = file_line_pattern.match(line)
            if file_match:
                raw_path = file_match.group(1)
                line_num = int(file_match.group(2))
                rest = file_match.group(3) or ""

                # Update current file for .c/.s files
                if any(raw_path.endswith(ext) for ext in ['.c', '.s', '.S']):
                    normalized = normalize_path(raw_path)
                    if normalized != current_file and current_func:
                        # Save function to old file, start fresh
                        files[current_file].append(current_func)
                        current_func = Function(name=current_func.name, address=current_func.address)
                    current_file = normalized

                # Store as pending source (text comes on next line usually, or is inline)
                source_text = rest.strip() if rest.strip() else ""
                # Strip discriminator annotations from inline text
                source_text = re.sub(r'\s*\(discriminator\s+\d+\)', '', source_text)
                pending_source_lines.append((normalize_path(raw_path), line_num, source_text))
                continue

            # Assembly instruction
            asm_match = asm_pattern.match(line)
            if asm_match and current_func:
                addr = int(asm_match.group(1), 16)
                hex_bytes = asm_match.group(2).strip()
                instr = asm_match.group(3)

                asm_line = AsmLine(
                    address=addr,
                    hex_bytes=hex_bytes,
                    instruction=instr,
                    raw_line=line
                )

                # If we have pending source, create a new source block
                if pending_source_lines:
                    # Use the most recent source reference
                    src_file, src_line, src_text = pending_source_lines[-1]
                    # Preserve original indentation from source file
                    current_source_block = SourceBlock(
                        file_path=src_file,
                        line_number=src_line,
                        source_text=src_text
                    )
                    current_func.source_blocks.append(current_source_block)
                    pending_source_lines = []

                # Add asm to current source block
                if current_source_block:
                    current_source_block.asm_lines.append(asm_line)
                else:
                    # No source context yet, create an orphan block
                    current_source_block = SourceBlock(
                        file_path=current_file,
                        line_number=0,
                        source_text=""
                    )
                    current_source_block.asm_lines.append(asm_line)
                    current_func.source_blocks.append(current_source_block)
                continue

            # Source code line - any line not matched by patterns above is source text
            # (indented C source, comments, etc.)
            # Preserve the line as-is to maintain indentation; we'll dedent later
            if current_func and line.strip():
                # If we have a pending source reference, append this text to it
                if pending_source_lines:
                    file_path, line_num, existing_text = pending_source_lines[-1]
                    if existing_text:
                        # Append to existing (multiline source)
                        pending_source_lines[-1] = (file_path, line_num, existing_text + '\n' + line)
                    else:
                        pending_source_lines[-1] = (file_path, line_num, line)
                elif current_source_block and not current_source_block.source_text:
                    # No pending, but current block has no text - use this
                    current_source_block.source_text = line

    # Don't forget last function
    if current_func:
        files[current_file].append(current_func)

    return dict(files)


def generate_html(filename: str, functions: list[Function], all_files: list[str], binary_name: str,
                  profile: Optional[ProfileData] = None) -> str:
    """Generate HTML with dual-view support."""

    # Build sidebar
    sidebar_items = []
    for f in sorted(all_files):
        display = "(unknown)" if f == "_unknown_" else get_display_name(f)
        href = ("_unknown_" if f == "_unknown_" else get_display_name(f)) + ".html"
        is_current = (f == filename)
        class_attr = ' class="current"' if is_current else ''
        sidebar_items.append(f'<a href="{href}"{class_attr}>{html.escape(display)}</a>')

    sidebar_html = '\n'.join(sidebar_items)

    # Build function data for JavaScript
    functions_json = []
    for func in functions:
        blocks = []
        for sb in func.source_blocks:
            asm_data = []
            for asm in sb.asm_lines:
                asm_data.append({
                    'addr': f'{asm.address:08x}',
                    'hex': asm.hex_bytes,
                    'instr': asm.instruction,
                    'cycles': asm.cycles
                })
            blocks.append({
                'file': sb.file_path,
                'line': sb.line_number,
                'src': sb.source_text,
                'asm': asm_data
            })
        functions_json.append({
            'name': func.name,
            'addr': f'{func.address:08x}',
            'blocks': blocks
        })

    display_name = get_display_name(filename) if filename != "_unknown_" else "(unknown source)"

    # Profile data for JavaScript
    has_profile_json = 'true' if profile else 'false'
    total_cycles_json = str(profile.total_cycles) if profile else '0'

    return f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html.escape(display_name)} - Disassembly Explorer</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}

        /* === Premium Dark Design System === */
        :root {{
            /* Layered backgrounds */
            --bg-body: #0A0A0B;
            --bg-surface: #161618;
            --bg-highlight: #232326;

            /* Borders */
            --border-subtle: #333333;
            --border-hover: #555555;
            --border-column: #2A2A2D;

            /* Text colors */
            --text-primary: #FFFFFF;
            --text-secondary: #A1A1AA;
            --text-tertiary: #71717A;
            --text-dim: #52525B;

            /* Accent */
            --accent-green: #22C55E;
            --accent-blue: #3B82F6;

            /* Selection */
            --selection-bg: rgba(59, 130, 246, 0.1);
            --selection-border: var(--accent-blue);

            /* GitHub Dark Dimmed syntax colors (softer pastels) */
            --syntax-function: #8DDB8C;
            --syntax-keyword: #A5D6FF;
            --syntax-string: #96D0FF;
            --syntax-comment: #768390;
            --syntax-number: #F0B27A;
            --syntax-register: #D2A8FF;

            /* Heat map colors */
            --heat-hot: #F87171;
            --heat-warm: #FBBF24;

            /* Fonts */
            --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            --font-mono: 'JetBrains Mono', 'SF Mono', Monaco, Consolas, monospace;
        }}

        body {{
            font-family: var(--font-sans);
            display: flex;
            height: 100vh;
            background: var(--bg-body);
            color: var(--text-secondary);
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
        }}

        /* === Sidebar (File Navigator) === */
        .sidebar {{
            width: 200px;
            background: var(--bg-surface);
            border-right: 1px solid var(--border-subtle);
            overflow-y: auto;
            padding: 0;
            flex-shrink: 0;
        }}
        .sidebar-header {{
            padding: 12px 16px;
            font-size: 10px;
            font-weight: 500;
            color: var(--text-tertiary);
            text-transform: uppercase;
            letter-spacing: 0.1em;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .sidebar a {{
            display: block;
            padding: 6px 16px;
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 12px;
            font-family: var(--font-mono);
            border-left: 2px solid transparent;
            transition: all 0.15s ease;
        }}
        .sidebar a:hover {{
            background: var(--bg-highlight);
            color: var(--text-primary);
        }}
        .sidebar a.current {{
            background: var(--selection-bg);
            color: var(--text-primary);
            border-left-color: var(--accent-green);
        }}
        .search {{
            padding: 8px 12px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .search input {{
            width: 100%;
            padding: 8px 12px;
            background: var(--bg-highlight);
            border: 1px solid var(--border-subtle);
            border-radius: 6px;
            color: var(--text-primary);
            font-size: 12px;
            font-family: var(--font-mono);
            transition: border-color 0.15s ease;
        }}
        .search input:focus {{
            outline: none;
            border-color: var(--text-tertiary);
        }}
        .search input::placeholder {{ color: var(--text-dim); }}

        /* === Main Content Area === */
        .main {{
            flex: 1;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            background: var(--bg-body);
        }}

        /* === Toolbar === */
        .toolbar {{
            height: 44px;
            min-height: 44px;
            background: var(--bg-surface);
            padding: 0 16px;
            display: flex;
            align-items: center;
            gap: 16px;
            border-bottom: 1px solid var(--border-subtle);
        }}

        /* Breadcrumb */
        .breadcrumb {{
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 13px;
            font-family: var(--font-sans);
            color: var(--text-secondary);
        }}
        .breadcrumb-sep {{
            color: var(--text-dim);
            font-size: 10px;
        }}
        .breadcrumb-item {{
            color: var(--text-tertiary);
        }}
        .breadcrumb-item.current {{
            color: var(--text-primary);
            font-weight: 500;
        }}

        /* Segmented Control */
        .segmented-control {{
            display: flex;
            background: var(--bg-highlight);
            border-radius: 8px;
            padding: 3px;
            margin-left: auto;
            border: 1px solid var(--border-subtle);
        }}
        .segmented-control button {{
            background: transparent;
            border: none;
            color: var(--text-tertiary);
            padding: 6px 14px;
            font-size: 12px;
            font-weight: 500;
            font-family: var(--font-sans);
            cursor: pointer;
            border-radius: 6px;
            transition: all 0.15s ease;
        }}
        .segmented-control button:hover {{
            color: var(--text-secondary);
        }}
        .segmented-control button.active {{
            background: var(--bg-surface);
            color: var(--text-primary);
            box-shadow: 0 1px 3px rgba(0,0,0,0.3);
        }}

        .toolbar-checkbox {{
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 12px;
            color: var(--text-tertiary);
            margin-left: 12px;
        }}
        .toolbar-checkbox input {{
            accent-color: var(--accent-green);
        }}

        /* === Content Area === */
        .content {{
            flex: 1;
            overflow: auto;
            padding: 0;
            background: var(--bg-body);
        }}

        /* === Function Block === */
        .function {{
            border-bottom: 1px solid var(--border-subtle);
        }}
        .func-header {{
            background: var(--bg-surface);
            padding: 8px 16px;
            font-family: var(--font-mono);
            font-size: 12px;
            line-height: 1.4;
            color: var(--syntax-function);
            font-weight: 500;
            position: sticky;
            top: 0;
            z-index: 10;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .func-header .addr {{
            color: var(--text-dim);
            font-weight: 400;
            margin-right: 12px;
        }}

        /* === Source Line (Section Header) === */
        .source-line {{
            background: var(--bg-highlight);
            padding: 4px 0;
            font-family: var(--font-mono);
            font-size: 12px;
            line-height: 1.4;
            cursor: pointer;
            display: grid;
            grid-template-columns: 20px 120px 1fr;
            align-items: start;
            border-bottom: 1px solid var(--border-subtle);
            border-left: 3px solid transparent;
            transition: all 0.1s ease;
        }}
        .source-line.has-profile {{
            grid-template-columns: 20px 70px 120px 1fr;
        }}
        .source-line:hover {{
            background: var(--bg-surface);
        }}
        .source-line.selected {{
            background: var(--selection-bg) !important;
            border-left-color: var(--selection-border);
        }}
        .source-line .toggle {{
            color: var(--text-dim);
            text-align: center;
            font-size: 10px;
            padding-top: 2px;
        }}
        .source-line .cycles {{
            color: var(--text-tertiary);
            text-align: right;
            padding-right: 12px;
            border-right: 1px solid var(--border-column);
        }}
        .source-line .cycles.hot {{
            color: var(--heat-hot);
            font-weight: 500;
        }}
        .source-line .cycles.warm {{
            color: var(--heat-warm);
        }}
        .source-line .line-num {{
            color: var(--text-dim);
            text-align: right;
            padding-right: 12px;
            border-right: 1px solid var(--border-column);
            user-select: none;
        }}
        .source-line .code {{
            color: var(--text-primary);
            font-weight: 500;
            white-space: pre;
            overflow: hidden;
            text-overflow: ellipsis;
            padding-left: 12px;
        }}

        /* === Assembly Grid Layout (Spreadsheet Style) === */
        .asm-line {{
            display: grid;
            grid-template-columns: 80px 100px 1fr;
            padding: 3px 0;
            font-family: var(--font-mono);
            font-size: 12px;
            line-height: 1.4;
            border-bottom: 1px solid var(--border-column);
            background: var(--bg-body);
            border-left: 3px solid transparent;
            transition: all 0.1s ease;
        }}
        .asm-line.has-profile {{
            grid-template-columns: 80px 70px 100px 1fr;
        }}
        .asm-line:hover {{
            background: var(--bg-surface);
        }}
        .asm-line.selected {{
            background: var(--selection-bg) !important;
            border-left-color: var(--selection-border);
        }}
        .asm-line .addr {{
            color: var(--text-dim);
            text-align: right;
            padding-right: 12px;
            border-right: 1px solid var(--border-column);
        }}
        .asm-line .cycles {{
            color: var(--text-tertiary);
            text-align: right;
            padding-right: 12px;
            border-right: 1px solid var(--border-column);
        }}
        .asm-line .cycles.hot {{
            color: var(--heat-hot);
            font-weight: 500;
        }}
        .asm-line .cycles.warm {{
            color: var(--heat-warm);
        }}
        .asm-line .hex {{
            color: var(--text-dim);
            padding-right: 12px;
            border-right: 1px solid var(--border-column);
        }}
        .asm-line .instr {{
            color: var(--text-secondary);
            padding-left: 12px;
        }}

        /* === Block Containers === */
        .block {{ margin: 0; }}
        .asm-group {{ }}
        .asm-group.collapsed {{ display: none; }}
        .source-group {{ }}
        .source-group.collapsed {{ display: none; }}

        .asm-block {{
            cursor: pointer;
        }}

        /* === View Modes === */
        .view-source .asm-group {{ }}
        .view-machine .source-group {{ background: var(--bg-highlight); }}

        /* === Row Selection (full edge-to-edge) === */
        .content *:focus {{
            outline: none;
        }}
    </style>
</head>
<body>
    <nav class="sidebar">
        <div class="sidebar-header">Files</div>
        <div class="search">
            <input type="text" id="search" placeholder="Filter..." oninput="filterFiles()">
        </div>
        <div id="file-list">
            {sidebar_html}
        </div>
    </nav>
    <div class="main">
        <div class="toolbar">
            <div class="breadcrumb">
                <span class="breadcrumb-item">{html.escape(binary_name)}</span>
                <span class="breadcrumb-sep">&#9656;</span>
                <span class="breadcrumb-item current">{html.escape(display_name)}</span>
            </div>
            <div class="segmented-control">
                <button id="btn-source" class="active" onclick="setView('source')">Source Order</button>
                <button id="btn-machine" onclick="setView('machine')">Machine Order</button>
            </div>
            <label class="toolbar-checkbox">
                <input type="checkbox" id="expand-all" checked onchange="toggleExpandAll()">
                Expand
            </label>
        </div>
        <div class="content" id="content">
            <!-- Content rendered by JavaScript -->
        </div>
    </div>
    <script>
const FUNCTIONS = {json.dumps(functions_json)};
const HAS_PROFILE = {has_profile_json};
const TOTAL_CYCLES = {total_cycles_json};
let currentView = 'source';
let expandAll = true;

function escapeHtml(text) {{
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}}

function formatCycles(n) {{
    if (n === 0) return '';
    if (n >= 1e9) return (n/1e9).toFixed(1) + 'B';
    if (n >= 1e6) return (n/1e6).toFixed(1) + 'M';
    if (n >= 1e3) return (n/1e3).toFixed(1) + 'K';
    return n.toString();
}}

function getCycleClass(cycles) {{
    if (!HAS_PROFILE || cycles === 0) return '';
    const pct = cycles / TOTAL_CYCLES;
    if (pct >= 0.01) return 'hot';    // >= 1% of total
    if (pct >= 0.001) return 'warm';  // >= 0.1% of total
    return '';
}}

function renderSourceView() {{
    let html = '';
    for (const func of FUNCTIONS) {{
        html += `<div class="function">`;
        html += `<div class="func-header"><span class="addr">${{func.addr}}</span>${{escapeHtml(func.name)}}</div>`;

        // Group blocks by source line number and merge assembly
        // This handles compiler instruction reordering (GCC -O2)
        const lineMap = new Map();  // line -> {{src, file, asm: []}}
        const noLineAsm = [];       // assembly without source attribution

        for (const block of func.blocks) {{
            if (block.line > 0) {{
                const key = block.line;
                if (!lineMap.has(key)) {{
                    lineMap.set(key, {{
                        src: block.src,
                        file: block.file,
                        line: block.line,
                        asm: []
                    }});
                }}
                // Merge assembly from duplicate source lines
                for (const a of block.asm) {{
                    lineMap.get(key).asm.push(a);
                }}
            }} else {{
                // No source line - collect separately
                for (const a of block.asm) {{
                    noLineAsm.push(a);
                }}
            }}
        }}

        // Sort by line number
        const sortedBlocks = Array.from(lineMap.values()).sort((a, b) => a.line - b.line);

        // Sort assembly within each block by address (machine order)
        for (const block of sortedBlocks) {{
            block.asm.sort((a, b) => {{
                const addrA = parseInt(a.addr, 16);
                const addrB = parseInt(b.addr, 16);
                return addrA - addrB;
            }});
        }}

        // Render assembly without source attribution first (e.g., function prologue)
        if (noLineAsm.length > 0) {{
            const blockId = `${{func.addr}}-nosrc`;
            const expanded = expandAll ? '' : 'collapsed';
            const blockCycles = noLineAsm.reduce((sum, a) => sum + (a.cycles || 0), 0);
            const profileClass = HAS_PROFILE ? 'has-profile' : '';
            const cycleClass = getCycleClass(blockCycles);

            html += `<div class="block">`;
            html += `<div class="source-line ${{profileClass}}" onclick="toggleBlock('${{blockId}}')" tabindex="0">`;
            html += `<span class="toggle">${{expandAll ? '&#9662;' : '&#9656;'}}</span>`;
            if (HAS_PROFILE) {{
                html += `<span class="cycles ${{cycleClass}}">${{formatCycles(blockCycles)}}</span>`;
            }}
            html += `<span class="line-num"></span>`;
            html += `<span class="code" style="color: var(--text-muted); font-style: italic;">(no source)</span>`;
            html += `</div>`;
            html += `<div class="asm-group ${{expanded}}" id="${{blockId}}">`;
            for (const asm of noLineAsm) {{
                const cycleClass = getCycleClass(asm.cycles || 0);
                html += `<div class="asm-line ${{profileClass}}" tabindex="0">`;
                html += `<span class="addr">${{asm.addr}}</span>`;
                if (HAS_PROFILE) {{
                    html += `<span class="cycles ${{cycleClass}}">${{formatCycles(asm.cycles || 0)}}</span>`;
                }}
                html += `<span class="hex">${{asm.hex}}</span>`;
                html += `<span class="instr">${{escapeHtml(asm.instr)}}</span>`;
                html += `</div>`;
            }}
            html += `</div></div>`;
        }}

        // Render sorted source blocks
        for (let i = 0; i < sortedBlocks.length; i++) {{
            const block = sortedBlocks[i];
            const blockId = `${{func.addr}}-${{block.line}}`;
            const hasAsm = block.asm.length > 0;
            const expanded = expandAll ? '' : 'collapsed';

            // Calculate total cycles for this source block
            const blockCycles = block.asm.reduce((sum, a) => sum + (a.cycles || 0), 0);

            html += `<div class="block">`;

            // Source line (section header style)
            const toggleIcon = hasAsm ? (expandAll ? '&#9662;' : '&#9656;') : '';
            const profileClass = HAS_PROFILE ? 'has-profile' : '';
            const cycleClass = getCycleClass(blockCycles);
            html += `<div class="source-line ${{profileClass}}" onclick="toggleBlock('${{blockId}}')" tabindex="0">`;
            html += `<span class="toggle">${{toggleIcon}}</span>`;
            if (HAS_PROFILE) {{
                html += `<span class="cycles ${{cycleClass}}">${{formatCycles(blockCycles)}}</span>`;
            }}
            html += `<span class="line-num">${{block.line}}</span>`;
            html += `<span class="code">${{escapeHtml(block.src || '')}}</span>`;
            html += `</div>`;

            // Assembly lines (grid layout)
            if (hasAsm) {{
                html += `<div class="asm-group ${{expanded}}" id="${{blockId}}">`;
                for (const asm of block.asm) {{
                    const cycleClass = getCycleClass(asm.cycles || 0);
                    html += `<div class="asm-line ${{profileClass}}" tabindex="0">`;
                    html += `<span class="addr">${{asm.addr}}</span>`;
                    if (HAS_PROFILE) {{
                        html += `<span class="cycles ${{cycleClass}}">${{formatCycles(asm.cycles || 0)}}</span>`;
                    }}
                    html += `<span class="hex">${{asm.hex}}</span>`;
                    html += `<span class="instr">${{escapeHtml(asm.instr)}}</span>`;
                    html += `</div>`;
                }}
                html += `</div>`;
            }}

            html += `</div>`;
        }}
        html += `</div>`;
    }}
    return html;
}}

function renderMachineView() {{
    let html = '';
    for (const func of FUNCTIONS) {{
        html += `<div class="function">`;
        html += `<div class="func-header"><span class="addr">${{func.addr}}</span>${{escapeHtml(func.name)}}</div>`;

        for (let i = 0; i < func.blocks.length; i++) {{
            const block = func.blocks[i];
            const blockId = `m-${{func.addr}}-${{i}}`;
            const hasSrc = block.src || block.line > 0;
            const expanded = expandAll ? '' : 'collapsed';

            html += `<div class="block asm-primary">`;

            // Assembly block (clickable to show source)
            if (block.asm.length > 0) {{
                html += `<div class="asm-block" onclick="toggleBlock('${{blockId}}')">`;
                for (const asm of block.asm) {{
                    const profileClass = HAS_PROFILE ? 'has-profile' : '';
                    const cycleClass = getCycleClass(asm.cycles || 0);
                    html += `<div class="asm-line ${{profileClass}}">`;
                    html += `<span class="addr">${{asm.addr}}</span>`;
                    if (HAS_PROFILE) {{
                        html += `<span class="cycles ${{cycleClass}}">${{formatCycles(asm.cycles || 0)}}</span>`;
                    }}
                    html += `<span class="hex">${{asm.hex}}</span>`;
                    html += `<span class="instr">${{escapeHtml(asm.instr)}}</span>`;
                    html += `</div>`;
                }}
                html += `</div>`;
            }}

            // Source context
            if (hasSrc) {{
                const blockCycles = block.asm.reduce((sum, a) => sum + (a.cycles || 0), 0);
                const profileClass = HAS_PROFILE ? 'has-profile' : '';
                const cycleClass = getCycleClass(blockCycles);
                // Only show first line of source in machine view (multi-line blocks look messy)
                const firstLine = (block.src || '').split('\\n')[0];
                html += `<div class="source-group ${{expanded}}" id="${{blockId}}">`;
                html += `<div class="source-line ${{profileClass}}">`;
                html += `<span class="toggle"></span>`;  // Empty toggle to match grid columns
                if (HAS_PROFILE) {{
                    html += `<span class="cycles ${{cycleClass}}">${{formatCycles(blockCycles)}}</span>`;
                }}
                html += `<span class="line-num">${{block.file}}:${{block.line}}</span>`;
                html += `<span class="code">${{escapeHtml(firstLine)}}</span>`;
                html += `</div>`;
                html += `</div>`;
            }}

            html += `</div>`;
        }}
        html += `</div>`;
    }}
    return html;
}}

function setView(view) {{
    currentView = view;
    document.getElementById('btn-source').classList.toggle('active', view === 'source');
    document.getElementById('btn-machine').classList.toggle('active', view === 'machine');
    render();
}}

function toggleBlock(id) {{
    const el = document.getElementById(id);
    if (el) {{
        el.classList.toggle('collapsed');
    }}
}}

function toggleExpandAll() {{
    expandAll = document.getElementById('expand-all').checked;
    render();
}}

function render() {{
    const content = document.getElementById('content');
    content.className = 'content view-' + currentView;
    content.innerHTML = currentView === 'source' ? renderSourceView() : renderMachineView();
}}

function filterFiles() {{
    const query = document.getElementById('search').value.toLowerCase();
    document.querySelectorAll('#file-list a').forEach(link => {{
        link.style.display = link.textContent.toLowerCase().includes(query) ? 'block' : 'none';
    }});
}}

// Initial render
render();
    </script>
</body>
</html>
'''


def generate_index_html(all_files: list[str], binary_name: str) -> str:
    """Generate the main index.html page."""
    total_files = len([f for f in all_files if f != "_unknown_"])

    file_items = []
    for f in sorted(all_files):
        display = "(unknown)" if f == "_unknown_" else get_display_name(f)
        href = ("_unknown_" if f == "_unknown_" else get_display_name(f)) + ".html"
        file_items.append(f'<a href="{href}">{html.escape(display)}</a>')

    files_html = '\n'.join(file_items)

    return f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Disassembly Explorer - {html.escape(binary_name)}</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}

        /* === Premium Dark Design System === */
        :root {{
            --bg-body: #0A0A0B;
            --bg-surface: #161618;
            --bg-highlight: #232326;
            --border-subtle: #333333;
            --border-hover: #555555;
            --text-primary: #FFFFFF;
            --text-secondary: #A1A1AA;
            --text-tertiary: #71717A;
            --text-dim: #52525B;
            --accent-green: #22C55E;
            --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            --font-mono: 'JetBrains Mono', 'SF Mono', Monaco, Consolas, monospace;
        }}

        body {{
            font-family: var(--font-sans);
            display: flex;
            height: 100vh;
            background: var(--bg-body);
            color: var(--text-secondary);
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
        }}

        .sidebar {{
            width: 200px;
            background: var(--bg-surface);
            border-right: 1px solid var(--border-subtle);
            overflow-y: auto;
            padding: 0;
            flex-shrink: 0;
        }}
        .sidebar-header {{
            padding: 12px 16px;
            font-size: 10px;
            font-weight: 500;
            color: var(--text-tertiary);
            text-transform: uppercase;
            letter-spacing: 0.1em;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .sidebar a {{
            display: block;
            padding: 6px 16px;
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 12px;
            font-family: var(--font-mono);
            border-left: 2px solid transparent;
            transition: all 0.15s ease;
        }}
        .sidebar a:hover {{
            background: var(--bg-highlight);
            color: var(--text-primary);
        }}
        .search {{
            padding: 8px 12px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .search input {{
            width: 100%;
            padding: 8px 12px;
            background: var(--bg-highlight);
            border: 1px solid var(--border-subtle);
            border-radius: 6px;
            color: var(--text-primary);
            font-size: 12px;
            font-family: var(--font-mono);
            transition: border-color 0.15s ease;
        }}
        .search input:focus {{
            outline: none;
            border-color: var(--text-tertiary);
        }}
        .search input::placeholder {{ color: var(--text-dim); }}

        .content {{
            flex: 1;
            overflow: auto;
            padding: 40px 48px;
        }}

        .header {{
            margin-bottom: 32px;
        }}
        .header h1 {{
            font-size: 1.75rem;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 6px;
            letter-spacing: -0.02em;
        }}
        .header .subtitle {{
            font-size: 0.875rem;
            color: var(--text-tertiary);
        }}

        .stats {{
            display: flex;
            gap: 20px;
            margin-bottom: 32px;
        }}
        .stat {{
            background: linear-gradient(to bottom right, #1a1a1a, #111111);
            padding: 20px 28px;
            border-radius: 12px;
            border: 1px solid var(--border-subtle);
        }}
        .stat-value {{
            font-size: 2rem;
            font-weight: 600;
            color: var(--accent-green);
            font-variant-numeric: tabular-nums;
        }}
        .stat-label {{
            font-size: 0.75rem;
            color: var(--text-tertiary);
            margin-top: 4px;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }}

        .intro {{
            background: linear-gradient(to bottom right, #1a1a1a, #111111);
            padding: 24px;
            border-radius: 12px;
            border: 1px solid var(--border-subtle);
            line-height: 1.6;
        }}
        .intro h2 {{
            font-size: 1rem;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 12px;
        }}
        .intro p {{
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-bottom: 10px;
        }}
        .intro p:last-child {{ margin-bottom: 0; }}
        .intro strong {{ color: var(--text-primary); }}
        .intro code {{
            background: var(--bg-highlight);
            padding: 2px 8px;
            border-radius: 4px;
            font-family: var(--font-mono);
            font-size: 0.8rem;
            border: 1px solid var(--border-subtle);
        }}
    </style>
</head>
<body>
    <nav class="sidebar">
        <div class="sidebar-header">Files</div>
        <div class="search">
            <input type="text" placeholder="Filter..." oninput="filterFiles(this.value)">
        </div>
        <div id="file-list">{files_html}</div>
    </nav>
    <div class="content">
        <div class="header">
            <h1>Disassembly Explorer</h1>
            <p class="subtitle">{html.escape(binary_name)}</p>
        </div>
        <div class="stats">
            <div class="stat">
                <div class="stat-value">{total_files}</div>
                <div class="stat-label">Source Files</div>
            </div>
        </div>
        <div class="intro">
            <h2>View Modes</h2>
            <p><strong>Source Order</strong> — C code with expandable assembly blocks underneath.</p>
            <p><strong>Machine Order</strong> — Assembly with expandable source context.</p>
            <p>Each file is available as <code>filename.c.html</code> for independent fetching.</p>
        </div>
    </div>
    <script>
        function filterFiles(query) {{
            query = query.toLowerCase();
            document.querySelectorAll('#file-list a').forEach(link => {{
                link.style.display = link.textContent.toLowerCase().includes(query) ? 'block' : 'none';
            }});
        }}
    </script>
</body>
</html>
'''


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Generate browsable disassembly HTML from objdump output')
    parser.add_argument('input_lst', help='Input .lst file from objdump -d -S')
    parser.add_argument('output_dir', help='Output directory for HTML files')
    parser.add_argument('--profile', '-p', help='Profile JSON file with per-address cycle counts')
    args = parser.parse_args()

    input_file = args.input_lst
    output_dir = args.output_dir
    os.makedirs(output_dir, exist_ok=True)

    print(f"Parsing {input_file}...", file=sys.stderr)
    files = parse_objdump(input_file)

    # Filter out GCC library files (soft-float, etc.) - they're huge and not useful
    library_patterns = ['lb1sf68', 'libgcc', 'crtbegin', 'crtend']
    filtered_files = {}
    for filepath, functions in files.items():
        if any(pattern in filepath for pattern in library_patterns):
            print(f"  Skipping library file: {filepath}", file=sys.stderr)
            continue
        filtered_files[filepath] = functions
    files = filtered_files

    print(f"Found {len(files)} source files", file=sys.stderr)

    # Load profile data if provided
    profile: Optional[ProfileData] = None
    if args.profile:
        print(f"Loading profile from {args.profile}...", file=sys.stderr)
        profile = load_profile(args.profile)
        if profile:
            print(f"  {len(profile.address_cycles)} addresses, {profile.total_cycles:,} total cycles", file=sys.stderr)
            annotate_with_profile(files, profile)
        else:
            print(f"  Warning: Failed to load profile", file=sys.stderr)

    binary_name = os.path.basename(input_file).replace('.lst', '.elf')
    all_files = list(files.keys())

    for filepath, functions in files.items():
        if filepath == "_unknown_":
            output_name = "_unknown_.html"
        else:
            output_name = get_display_name(filepath) + ".html"

        output_path = os.path.join(output_dir, output_name)
        html_content = generate_html(filepath, functions, all_files, binary_name, profile)

        with open(output_path, 'w') as f:
            f.write(html_content)

        total_blocks = sum(len(func.source_blocks) for func in functions)
        print(f"  {output_name}: {len(functions)} functions, {total_blocks} blocks", file=sys.stderr)

    index_path = os.path.join(output_dir, "index.html")
    with open(index_path, 'w') as f:
        f.write(generate_index_html(all_files, binary_name))
    print(f"  index.html", file=sys.stderr)

    print(f"Done! Output in {output_dir}/", file=sys.stderr)


if __name__ == "__main__":
    main()
