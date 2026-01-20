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
            # Note: objdump indents source with spaces, so we strip and preserve content
            if current_func and line.strip():
                stripped = line.strip()
                # If we have a pending source reference, append this text to it
                if pending_source_lines:
                    file_path, line_num, existing_text = pending_source_lines[-1]
                    if existing_text:
                        # Append to existing (multiline source)
                        pending_source_lines[-1] = (file_path, line_num, existing_text + '\n' + stripped)
                    else:
                        pending_source_lines[-1] = (file_path, line_num, stripped)
                elif current_source_block and not current_source_block.source_text:
                    # No pending, but current block has no text - use this
                    current_source_block.source_text = stripped

    # Don't forget last function
    if current_func:
        files[current_file].append(current_func)

    return dict(files)


def generate_html(filename: str, functions: list[Function], all_files: list[str], binary_name: str) -> str:
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
                    'instr': asm.instruction
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

    return f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html.escape(display_name)} - Disassembly Explorer</title>
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}
        body {{
            font-family: system-ui, -apple-system, sans-serif;
            display: flex;
            height: 100vh;
            background: #1e1e1e;
            color: #d4d4d4;
        }}
        .sidebar {{
            width: 200px;
            background: #252526;
            border-right: 1px solid #3c3c3c;
            overflow-y: auto;
            padding: 12px 0;
            flex-shrink: 0;
        }}
        .sidebar h2 {{
            color: #888;
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            padding: 8px 12px;
        }}
        .sidebar a {{
            display: block;
            padding: 4px 12px;
            color: #ccc;
            text-decoration: none;
            font-size: 12px;
            font-family: 'SF Mono', Consolas, monospace;
        }}
        .sidebar a:hover {{ background: #2a2d2e; }}
        .sidebar a.current {{ background: #094771; color: #fff; }}
        .main {{
            flex: 1;
            display: flex;
            flex-direction: column;
            overflow: hidden;
        }}
        .toolbar {{
            background: #333;
            padding: 8px 16px;
            display: flex;
            align-items: center;
            gap: 16px;
            border-bottom: 1px solid #444;
        }}
        .toolbar h1 {{
            font-size: 14px;
            color: #569cd6;
            font-family: 'SF Mono', Consolas, monospace;
        }}
        .toolbar .view-toggle {{
            display: flex;
            gap: 4px;
        }}
        .toolbar button {{
            background: #444;
            border: 1px solid #555;
            color: #ccc;
            padding: 4px 12px;
            font-size: 12px;
            cursor: pointer;
            border-radius: 3px;
        }}
        .toolbar button:hover {{ background: #505050; }}
        .toolbar button.active {{ background: #094771; border-color: #094771; color: #fff; }}
        .toolbar label {{
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 12px;
            color: #aaa;
        }}
        .content {{
            flex: 1;
            overflow: auto;
            padding: 0;
        }}
        .function {{
            border-bottom: 1px solid #333;
        }}
        .func-header {{
            background: #2d2d2d;
            padding: 8px 16px;
            font-family: 'SF Mono', Consolas, monospace;
            font-size: 13px;
            color: #dcdcaa;
            font-weight: bold;
            position: sticky;
            top: 0;
            z-index: 10;
        }}
        .func-header .addr {{ color: #808080; font-weight: normal; margin-right: 12px; }}
        .block {{
            border-left: 3px solid transparent;
            margin: 0;
        }}
        .block:hover {{ background: #252525; }}
        .block.src-primary {{ border-left-color: #4ec9b0; }}
        .block.asm-primary {{ border-left-color: #569cd6; }}
        .source-line {{
            padding: 2px 16px 2px 20px;
            font-family: 'SF Mono', Consolas, monospace;
            font-size: 12px;
            color: #9cdcfe;
            background: #1a2a1a;
            cursor: pointer;
            display: flex;
            align-items: flex-start;
        }}
        .source-line:hover {{ background: #1f3f1f; }}
        .source-line .line-num {{
            color: #6a9955;
            min-width: 50px;
            user-select: none;
        }}
        .source-line .code {{ flex: 1; white-space: pre; }}
        .source-line .toggle {{
            color: #666;
            margin-right: 8px;
            width: 16px;
            text-align: center;
        }}
        .asm-line {{
            padding: 1px 16px 1px 48px;
            font-family: 'SF Mono', Consolas, monospace;
            font-size: 11px;
            display: flex;
            gap: 12px;
            color: #9cdcfe;
        }}
        .asm-line:hover {{ background: #252530; }}
        .asm-line .addr {{ color: #6796e6; min-width: 70px; }}
        .asm-line .hex {{ color: #666; min-width: 100px; }}
        .asm-line .instr {{ color: #dcdcaa; flex: 1; }}
        .asm-block {{
            background: #1a1a2a;
            cursor: pointer;
        }}
        .asm-block:hover {{ background: #1f1f3f; }}
        .asm-block .asm-line {{ padding-left: 20px; }}
        .collapsed {{ display: none; }}
        .expand-indicator {{
            color: #666;
            font-size: 10px;
            padding: 2px 16px 2px 48px;
            cursor: pointer;
        }}
        .expand-indicator:hover {{ color: #aaa; }}

        /* Source-primary view */
        .view-source .asm-group {{ margin-left: 32px; }}
        .view-source .asm-group.collapsed {{ display: none; }}

        /* Machine-primary view */
        .view-machine .source-group {{ margin-left: 32px; background: #1a2a1a; }}
        .view-machine .source-group.collapsed {{ display: none; }}

        /* Search */
        .search {{
            padding: 8px 12px;
        }}
        .search input {{
            width: 100%;
            padding: 4px 8px;
            background: #3c3c3c;
            border: 1px solid #555;
            border-radius: 3px;
            color: #d4d4d4;
            font-size: 11px;
        }}
        .search input:focus {{ outline: none; border-color: #007acc; }}
    </style>
</head>
<body>
    <nav class="sidebar">
        <h2>Files</h2>
        <div class="search">
            <input type="text" id="search" placeholder="Filter..." oninput="filterFiles()">
        </div>
        <div id="file-list">
            {sidebar_html}
        </div>
    </nav>
    <div class="main">
        <div class="toolbar">
            <h1>{html.escape(display_name)}</h1>
            <div class="view-toggle">
                <button id="btn-source" class="active" onclick="setView('source')">Source Order</button>
                <button id="btn-machine" onclick="setView('machine')">Machine Order</button>
            </div>
            <label>
                <input type="checkbox" id="expand-all" checked onchange="toggleExpandAll()">
                Expand all
            </label>
        </div>
        <div class="content" id="content">
            <!-- Content rendered by JavaScript -->
        </div>
    </div>
    <script>
const FUNCTIONS = {json.dumps(functions_json)};
let currentView = 'source';
let expandAll = true;

function escapeHtml(text) {{
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}}

function renderSourceView() {{
    let html = '';
    for (const func of FUNCTIONS) {{
        html += `<div class="function">`;
        html += `<div class="func-header"><span class="addr">${{func.addr}}</span>${{escapeHtml(func.name)}}</div>`;

        for (let i = 0; i < func.blocks.length; i++) {{
            const block = func.blocks[i];
            const blockId = `${{func.addr}}-${{i}}`;
            const hasAsm = block.asm.length > 0;
            const expanded = expandAll ? '' : 'collapsed';

            html += `<div class="block src-primary">`;

            // Source line
            if (block.src || block.line > 0) {{
                const toggleIcon = hasAsm ? (expandAll ? '▼' : '▶') : '';
                html += `<div class="source-line" onclick="toggleBlock('${{blockId}}')">`;
                html += `<span class="toggle">${{toggleIcon}}</span>`;
                html += `<span class="line-num">${{block.line || ''}}</span>`;
                html += `<span class="code">${{escapeHtml(block.src || '')}}</span>`;
                html += `</div>`;
            }}

            // Assembly lines
            if (hasAsm) {{
                html += `<div class="asm-group ${{expanded}}" id="${{blockId}}">`;
                for (const asm of block.asm) {{
                    html += `<div class="asm-line">`;
                    html += `<span class="addr">${{asm.addr}}</span>`;
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
                    html += `<div class="asm-line">`;
                    html += `<span class="addr">${{asm.addr}}</span>`;
                    html += `<span class="hex">${{asm.hex}}</span>`;
                    html += `<span class="instr">${{escapeHtml(asm.instr)}}</span>`;
                    html += `</div>`;
                }}
                html += `</div>`;
            }}

            // Source context
            if (hasSrc) {{
                html += `<div class="source-group ${{expanded}}" id="${{blockId}}">`;
                html += `<div class="source-line">`;
                html += `<span class="line-num">${{block.file}}:${{block.line}}</span>`;
                html += `<span class="code">${{escapeHtml(block.src || '')}}</span>`;
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
        body {{
            font-family: system-ui, -apple-system, sans-serif;
            display: flex;
            height: 100vh;
            background: #1e1e1e;
            color: #d4d4d4;
        }}
        .sidebar {{
            width: 200px;
            background: #252526;
            border-right: 1px solid #3c3c3c;
            overflow-y: auto;
            padding: 12px 0;
        }}
        .sidebar h2 {{
            color: #888;
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
            padding: 8px 12px;
        }}
        .sidebar a {{
            display: block;
            padding: 4px 12px;
            color: #ccc;
            text-decoration: none;
            font-size: 12px;
            font-family: 'SF Mono', Consolas, monospace;
        }}
        .sidebar a:hover {{ background: #2a2d2e; }}
        .content {{
            flex: 1;
            overflow: auto;
            padding: 32px;
        }}
        h1 {{ color: #569cd6; font-size: 24px; margin-bottom: 8px; }}
        .subtitle {{ color: #808080; margin-bottom: 32px; }}
        .stats {{
            display: flex;
            gap: 24px;
            margin-bottom: 32px;
        }}
        .stat {{
            background: #252526;
            padding: 16px 24px;
            border-radius: 8px;
            border: 1px solid #3c3c3c;
        }}
        .stat-value {{ font-size: 32px; font-weight: bold; color: #4ec9b0; }}
        .stat-label {{ font-size: 12px; color: #808080; margin-top: 4px; }}
        .intro {{
            background: #252526;
            padding: 20px;
            border-radius: 8px;
            border: 1px solid #3c3c3c;
            line-height: 1.6;
        }}
        .intro h2 {{ color: #dcdcaa; font-size: 16px; margin-bottom: 12px; }}
        .intro p {{ color: #9cdcfe; font-size: 14px; margin-bottom: 12px; }}
        .intro code {{
            background: #1e1e1e;
            padding: 2px 6px;
            border-radius: 3px;
            font-family: 'SF Mono', Consolas, monospace;
        }}
        .search {{
            padding: 8px 12px;
        }}
        .search input {{
            width: 100%;
            padding: 4px 8px;
            background: #3c3c3c;
            border: 1px solid #555;
            border-radius: 3px;
            color: #d4d4d4;
            font-size: 11px;
        }}
    </style>
</head>
<body>
    <nav class="sidebar">
        <h2>Files</h2>
        <div class="search">
            <input type="text" placeholder="Filter..." oninput="filterFiles(this.value)">
        </div>
        <div id="file-list">{files_html}</div>
    </nav>
    <div class="content">
        <h1>Disassembly Explorer</h1>
        <p class="subtitle">{html.escape(binary_name)}</p>
        <div class="stats">
            <div class="stat">
                <div class="stat-value">{total_files}</div>
                <div class="stat-label">Source Files</div>
            </div>
        </div>
        <div class="intro">
            <h2>Features</h2>
            <p><strong>Source Order:</strong> View C code with expandable assembly blocks underneath.</p>
            <p><strong>Machine Order:</strong> View assembly with expandable source context.</p>
            <p><strong>For AI Agents:</strong> Each file is available as <code>filename.c.html</code> for independent fetching.</p>
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
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.lst> <output_dir>", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_dir = sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)

    print(f"Parsing {input_file}...", file=sys.stderr)
    files = parse_objdump(input_file)
    print(f"Found {len(files)} source files", file=sys.stderr)

    binary_name = os.path.basename(input_file).replace('.lst', '.elf')
    all_files = list(files.keys())

    for filepath, functions in files.items():
        if filepath == "_unknown_":
            output_name = "_unknown_.html"
        else:
            output_name = get_display_name(filepath) + ".html"

        output_path = os.path.join(output_dir, output_name)
        html_content = generate_html(filepath, functions, all_files, binary_name)

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
