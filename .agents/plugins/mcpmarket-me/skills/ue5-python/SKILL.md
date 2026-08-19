---
name: ue5-python
description: Comprehensive guide for developing UE5 Editor Python scripts with proper workflow and best practices. Use when the user wants to (1) write a UE5 Python script, (2) mentions writing a script in a UE5 project context, or (3) has a requirement that Claude identifies can be fulfilled by a UE5 Python script (e.g., batch processing game assets, automating editor tasks).
allowedTools:
  - tool: Read
    path: ${CLAUDE_PROJECT_DIR}/**
  - tool: Write
    path: ${CLAUDE_PROJECT_DIR}/**
  - tool: Edit
    path: ${CLAUDE_PROJECT_DIR}/**
  - tool: Glob
    path: ${CLAUDE_PROJECT_DIR}/**
  - tool: Grep
    path: ${CLAUDE_PROJECT_DIR}/**
allowedPrompts:
  - tool: Bash
    prompt: run python scripts in project directory
  - tool: Bash
    prompt: execute UE5 remote python scripts
  - tool: Bash
    prompt: take game screenshots
  - tool: Bash
    prompt: delete temporary files in project
hooks:
  PreToolUse:
    - matcher: Write|Edit
      hooks:
        - type: command
          command: |
            python -c "
            import sys, json, re
            try:
                data = json.load(sys.stdin)
                tool = data.get('tool_name', '')
                if tool not in ['Write', 'Edit']: sys.exit(0)
                fp = data.get('tool_input', {}).get('file_path', '')
                if not fp.endswith('.py'): sys.exit(0)
                content = data.get('tool_input', {}).get('content', '') if tool == 'Write' else ''
                if not content or 'import unreal' not in content: sys.exit(0)
                ops = [r'\.set_editor_property\(', r'.*save.*\(', r'\.set_editor_properties\(', r'\.modify\(']
                has_ops = any(re.search(p, content) for p in ops)
                if not has_ops: sys.exit(0)
                has_tx = bool(re.search(r'with\s+unreal\.ScopedEditorTransaction\s*\(', content) or re.search(r'unreal\.ScopedEditorTransaction\(', content))
                if not has_tx:
                    print(f'Transaction Check Failed: {fp}\n\nThis UE5 Python script modifies assets but does NOT use transactions.\n\nREQUIRED: Wrap in transaction:\n  with unreal.ScopedEditorTransaction(\"Desc\"):\n      asset.set_editor_property(...)\n\nWhy: Enables undo and rollback on failure.', file=sys.stderr)
                    sys.exit(2)
            except: pass
            "
  Stop:
    - matcher: "*"
      hooks:
        - type: prompt
          prompt: |
            Examining $ARGUMENTS, verify if all of the user's requirements have been fully completed:

            1. **Review all user requests** - Look at the initial request and any follow-up requirements
            2. **Check task completion**:
               - All requested scripts written and saved?
               - All scripts tested and verified working?
               - Visual verification done if needed (screenshots taken)?
               - All API validations passed?
               - All errors fixed and resolved?
               - All user questions answered?
            3. **Final cleanup** - Delete all failed screenshot attempts - only keep successful verification screenshots

            Respond with JSON: {"ok": true} to allow stopping, or {"ok": false, "reason": "your explanation"} to continue working.

            **Decision rules:**
            - Return {"ok": true} ONLY if all requirements are fully complete
            - Return {"ok": false, "reason": "your explanation"} if any required task is incomplete, unverified, or outstanding
            - If uncertain, return {"ok": false, "reason": "your explanation"} to ensure nothing is skipped
metadata:
  mcpmarket-version: 1.0.0
---
# UE5 Python Script Development Guide

A workflow-oriented guide for developing reliable UE5 Editor Python scripts.

## Prerequisites

[Critical] **ue5-visual** subagent and **ue-mcp** server must be available. If not present, refuse to use this skill.

CLI scripts for capture and diagnostics are in `scripts/`. See [Capture Scripts Reference](./references/capture-scripts.md) for details.

## Development Workflow

### Phase 1: Requirements

Clarify requirements with user using AskUserQuestions tool until you have 95% confidence in understanding their intentions.

### Phase 2: Planning

**Do not implement yet - just plan.**

[Critical] Every step implements and tests ONE script. Three types allowed:

#### Script Types

| Type | Purpose | Testing Steps |
|------|---------|---------------|
| **Scene-Setup** | Setup static scene (atmosphere, lighting, actors) | diagnostic -> orbital capture -> ue5-visual analysis |
| **Configuration** | Configure properties, tags, physics, collisions | diagnostic -> window capture -> ue5-visual analysis |
| **Integration Test** | Run scene in PIE mode | PIE capture -> ue5-visual analysis |

#### Testing Protocol

Each script step has three substeps:

- **x.1** - Run `asset-diagnostic.py` to check for issues, fix all errors/warnings
- **x.2** - Capture screenshots (orbital/window/PIE based on script type)
- **x.3** - **MANDATORY**: Use *ue5-visual* subagent to analyze screenshots

#### Example Plan

For "Create island level with two fighting robots":

```markdown
## Step 1: Scene-setup - create_island_level.py
Setup island, sea, atmosphere, light, and two robots
- Step 1.1: Run asset diagnostic, fix issues
- Step 1.2: Capture orbital screenshots
- Step 1.3: [MANDATORY] ue5-visual analysis

## Step 2: Configuration - configure_robots.py
Configure collisions, physics, attack abilities
- Step 2.1: Run asset diagnostic, fix issues
- Step 2.2: Capture window screenshots
- Step 2.3: [MANDATORY] ue5-visual analysis

## Step 3: Integration Test - run_in_pie.py
Setup PIE capture, play level, collect screenshots
- Step 3.1: [MANDATORY] ue5-visual analysis
```

[Critical] Add ALL substeps to todo list using TodoWrite.

#### Script Organization

Organize scripts in task-specific subdirectory:

```
${CLAUDE_PROJECT_DIR}/Scripts/<task_name>/
    ├── create_island_level.py
    ├── configure_robots.py
    ├── tmp/                    # Test/debug scripts
    └── screenshots/            # Verification screenshots
        ├── orbital/
        ├── editor/
        └── pie/
```

### Phase 3: Implementation

1. [Critical] Check [Common Pitfalls](./references/common-pitfals/) for related documents
2. Follow [Best Practices](./references/best-practices.md)
3. Reference example scripts:
   - [Add gameplay tag](./examples/add_gameplaytag_to_asset.py)
   - [Create blendspace](./examples/create_footwork_blendspace.py)
   - [Create level](./examples/create_sky_level.py)
   - [Customize atmosphere](./examples/create_dark_pyramid_level.py)
   - [Create blueprint with physics](./examples/create_punching_bag_blueprint.py)
   - [PIE screenshot capturer](./examples/pie_screenshot_capturer.py)

4. Use **ue-mcp** tool `editor.execute` to run scripts
5. Use **ue5-api-expert** to verify API usage when unsure

## Visual Verification

| Scenario | Script | Key Parameters |
|----------|--------|----------------|
| Static scene | `orbital-capture.py` | `preset`, `distance`, `target_x/y/z` |
| Blueprint/Asset | `window-capture.py` | `command`, `asset_path`, `tab` |
| PIE runtime | `pie-capture.py` | `command`, `interval`, `multi_angle` |

**Presets for orbital capture**: `orthographic` (6 views), `perspective` (4 views), `birdseye` (4 views), `all` (14 views)

See [Capture Scripts Reference](./references/capture-scripts.md) for full parameter documentation.

[Critical] Do NOT skip visual analysis. Use ue5-visual agent after every capture.

## Asset Diagnostic

Run before visual verification to identify issues:
- Use `asset_diagnostic` Python module via `editor.execute`
- Check for common issues in levels, actors, and blueprints

Fix all errors and warnings before proceeding. See [Asset Diagnostic Reference](./references/asset-diagnostic.md).

## Additional Resources

- [Best Practices](./references/best-practices.md) - Debug visualization, subsystems, transactions, asset registry
- [ExtraPythonAPIs Plugin](./references/extra-python-apis.md) - For socket/bone attachment in Blueprints
- [Capture Scripts Reference](./references/capture-scripts.md) - CLI parameter documentation
- [Editor Capture API](./references/editor-capture.md) - Python module API
- [Asset Diagnostic API](./references/asset-diagnostic.md) - Diagnostic module API
- [C++ Source Investigation](./references/cpp-source-investigation.md) - When Python API is insufficient
