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
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}

        /* === Apple System Dark Mode Palette === */
        :root {{
            --bg-app: #1E1E1E;
            --bg-sidebar: #292A30;
            --bg-toolbar: #323232;
            --bg-source-header: #000000;
            --border-row: #333333;
            --border-subtle: #3A3A3C;
            --selection-bg: #0060FA;
            --selection-text: #FFFFFF;
            --text-primary: #FFFFFF;
            --text-secondary: #CCCCCC;
            --text-muted: #8F8F8F;
            --text-dim: #666666;
            --accent-source: #FF79C6;
            --accent-keyword: #66D9EF;
            --accent-function: #A6E22E;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Helvetica Neue', sans-serif;
            display: flex;
            height: 100vh;
            background: var(--bg-app);
            color: var(--text-secondary);
            -webkit-font-smoothing: antialiased;
        }}

        /* === Sidebar (File Navigator) === */
        .sidebar {{
            width: 180px;
            background: var(--bg-sidebar);
            border-right: 1px solid var(--border-subtle);
            overflow-y: auto;
            padding: 0;
            flex-shrink: 0;
        }}
        .sidebar-header {{
            padding: 8px 12px;
            font-size: 11px;
            font-weight: 600;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.5px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .sidebar a {{
            display: block;
            padding: 3px 12px;
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 11px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
            border-bottom: 1px solid transparent;
        }}
        .sidebar a:hover {{ background: rgba(255,255,255,0.05); }}
        .sidebar a.current {{
            background: var(--selection-bg);
            color: var(--selection-text);
        }}
        .search {{
            padding: 6px 8px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .search input {{
            width: 100%;
            padding: 4px 8px;
            background: rgba(255,255,255,0.08);
            border: 1px solid var(--border-subtle);
            border-radius: 4px;
            color: var(--text-secondary);
            font-size: 11px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
        }}
        .search input:focus {{
            outline: none;
            border-color: var(--selection-bg);
            background: rgba(255,255,255,0.12);
        }}
        .search input::placeholder {{ color: var(--text-dim); }}

        /* === Main Content Area === */
        .main {{
            flex: 1;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            background: var(--bg-app);
        }}

        /* === NSToolbar Style === */
        .toolbar {{
            height: 28px;
            min-height: 28px;
            background: var(--bg-toolbar);
            padding: 0 12px;
            display: flex;
            align-items: center;
            gap: 12px;
            border-bottom: 1px solid var(--border-subtle);
        }}

        /* Breadcrumb / Jump Bar */
        .breadcrumb {{
            display: flex;
            align-items: center;
            gap: 4px;
            font-size: 11px;
            font-family: -apple-system, BlinkMacSystemFont, sans-serif;
            color: var(--text-secondary);
        }}
        .breadcrumb-sep {{
            color: var(--text-dim);
            font-size: 9px;
        }}
        .breadcrumb-item {{
            color: var(--text-secondary);
        }}
        .breadcrumb-item.current {{
            color: var(--text-primary);
        }}

        /* Segmented Control */
        .segmented-control {{
            display: flex;
            background: rgba(255,255,255,0.06);
            border-radius: 5px;
            padding: 1px;
            margin-left: auto;
        }}
        .segmented-control button {{
            background: transparent;
            border: none;
            color: var(--text-muted);
            padding: 3px 10px;
            font-size: 11px;
            font-family: -apple-system, BlinkMacSystemFont, sans-serif;
            cursor: pointer;
            border-radius: 4px;
            transition: all 0.15s ease;
        }}
        .segmented-control button:hover {{
            color: var(--text-secondary);
        }}
        .segmented-control button.active {{
            background: rgba(255,255,255,0.15);
            color: var(--text-primary);
            box-shadow: 0 1px 2px rgba(0,0,0,0.3);
        }}

        .toolbar-checkbox {{
            display: flex;
            align-items: center;
            gap: 4px;
            font-size: 11px;
            color: var(--text-muted);
            margin-left: 8px;
        }}
        .toolbar-checkbox input {{
            accent-color: var(--selection-bg);
        }}

        /* === Content Area === */
        .content {{
            flex: 1;
            overflow: auto;
            padding: 0;
            background: var(--bg-app);
        }}

        /* === Function Block === */
        .function {{
            border-bottom: 1px solid var(--border-row);
        }}
        .func-header {{
            background: var(--bg-sidebar);
            padding: 4px 12px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
            font-size: 11px;
            line-height: 1.3;
            color: var(--accent-function);
            font-weight: 600;
            position: sticky;
            top: 0;
            z-index: 10;
            border-bottom: 1px solid var(--border-row);
        }}
        .func-header .addr {{
            color: var(--text-dim);
            font-weight: normal;
            margin-right: 8px;
        }}

        /* === Source Line (Section Header) === */
        .source-line {{
            background: var(--bg-source-header);
            padding: 3px 0;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
            font-size: 11px;
            line-height: 1.3;
            cursor: pointer;
            display: grid;
            grid-template-columns: 16px 150px 1fr;
            align-items: start;
            border-bottom: 1px solid var(--border-row);
        }}
        .source-line.has-profile {{
            grid-template-columns: 16px 70px 150px 1fr;
        }}
        .source-line:hover {{ background: #0a0a0a; }}
        .source-line.selected {{
            background: var(--selection-bg) !important;
        }}
        .source-line.selected * {{
            color: var(--selection-text) !important;
        }}
        .source-line .toggle {{
            color: var(--text-dim);
            text-align: center;
            font-size: 9px;
            padding-top: 1px;
        }}
        .source-line .cycles {{
            color: var(--text-muted);
            text-align: right;
            padding-right: 8px;
        }}
        .source-line .cycles.hot {{
            color: #FF6B6B;
            font-weight: 600;
        }}
        .source-line .cycles.warm {{
            color: #FFB347;
        }}
        .source-line .line-num {{
            color: var(--text-dim);
            text-align: right;
            padding-right: 8px;
            user-select: none;
        }}
        .source-line .code {{
            color: var(--text-primary);
            font-weight: 600;
            white-space: pre;
            overflow: hidden;
            text-overflow: ellipsis;
        }}

        /* === Assembly Grid Layout === */
        .asm-line {{
            display: grid;
            grid-template-columns: 70px 80px 1fr;
            padding: 1px 0;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
            font-size: 11px;
            line-height: 1.3;
            border-bottom: 1px solid var(--border-row);
            background: var(--bg-app);
        }}
        .asm-line.has-profile {{
            grid-template-columns: 70px 70px 80px 1fr;
        }}
        .asm-line:hover {{ background: #252525; }}
        .asm-line.selected {{
            background: var(--selection-bg) !important;
        }}
        .asm-line.selected * {{
            color: var(--selection-text) !important;
        }}
        .asm-line .addr {{
            color: var(--text-dim);
            text-align: right;
            padding-right: 8px;
        }}
        .asm-line .cycles {{
            color: var(--text-muted);
            text-align: right;
            padding-right: 8px;
        }}
        .asm-line .cycles.hot {{
            color: #FF6B6B;
            font-weight: 600;
        }}
        .asm-line .cycles.warm {{
            color: #FFB347;
        }}
        .asm-line .hex {{
            color: var(--text-dim);
            padding-right: 8px;
        }}
        .asm-line .instr {{
            color: var(--text-muted);
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
        .view-machine .source-group {{ background: var(--bg-source-header); }}

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
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}

        :root {{
            --bg-app: #1E1E1E;
            --bg-sidebar: #292A30;
            --bg-card: #2C2C2E;
            --border-subtle: #3A3A3C;
            --selection-bg: #0060FA;
            --text-primary: #FFFFFF;
            --text-secondary: #CCCCCC;
            --text-muted: #8F8F8F;
            --accent-green: #32D74B;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Helvetica Neue', sans-serif;
            display: flex;
            height: 100vh;
            background: var(--bg-app);
            color: var(--text-secondary);
            -webkit-font-smoothing: antialiased;
        }}

        .sidebar {{
            width: 180px;
            background: var(--bg-sidebar);
            border-right: 1px solid var(--border-subtle);
            overflow-y: auto;
            padding: 0;
            flex-shrink: 0;
        }}
        .sidebar-header {{
            padding: 8px 12px;
            font-size: 11px;
            font-weight: 600;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.5px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .sidebar a {{
            display: block;
            padding: 3px 12px;
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 11px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
        }}
        .sidebar a:hover {{ background: rgba(255,255,255,0.05); }}
        .search {{
            padding: 6px 8px;
            border-bottom: 1px solid var(--border-subtle);
        }}
        .search input {{
            width: 100%;
            padding: 4px 8px;
            background: rgba(255,255,255,0.08);
            border: 1px solid var(--border-subtle);
            border-radius: 4px;
            color: var(--text-secondary);
            font-size: 11px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
        }}
        .search input:focus {{
            outline: none;
            border-color: var(--selection-bg);
        }}
        .search input::placeholder {{ color: #666; }}

        .content {{
            flex: 1;
            overflow: auto;
            padding: 24px 32px;
        }}

        .header {{
            margin-bottom: 24px;
        }}
        .header h1 {{
            font-size: 20px;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 4px;
        }}
        .header .subtitle {{
            font-size: 12px;
            color: var(--text-muted);
        }}

        .stats {{
            display: flex;
            gap: 16px;
            margin-bottom: 24px;
        }}
        .stat {{
            background: var(--bg-card);
            padding: 12px 20px;
            border-radius: 8px;
            border: 1px solid var(--border-subtle);
        }}
        .stat-value {{
            font-size: 28px;
            font-weight: 600;
            color: var(--accent-green);
            font-variant-numeric: tabular-nums;
        }}
        .stat-label {{
            font-size: 11px;
            color: var(--text-muted);
            margin-top: 2px;
        }}

        .intro {{
            background: var(--bg-card);
            padding: 16px 20px;
            border-radius: 8px;
            border: 1px solid var(--border-subtle);
            line-height: 1.5;
        }}
        .intro h2 {{
            font-size: 13px;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 10px;
        }}
        .intro p {{
            font-size: 12px;
            color: var(--text-muted);
            margin-bottom: 8px;
        }}
        .intro p:last-child {{ margin-bottom: 0; }}
        .intro strong {{ color: var(--text-secondary); }}
        .intro code {{
            background: var(--bg-app);
            padding: 2px 6px;
            border-radius: 3px;
            font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;
            font-size: 11px;
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
            <p><strong>Source Order</strong> - C code with expandable assembly blocks underneath.</p>
            <p><strong>Machine Order</strong> - Assembly with expandable source context.</p>
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
