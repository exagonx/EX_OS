# GF_TEXTEDITOR

**GF_TEXTEDITOR** is a reusable full-screen text editor for Linux terminals, written entirely in ANSI C using **ncursesw** and **POSIX threads**. It is designed as a self-contained component that can be easily integrated into larger C applications while maintaining a clean, object-inspired architecture.

Unlike traditional procedural editors, the entire editor state—including documents, tabs, clipboard, undo history, syntax highlighting, threads, and configuration—is encapsulated inside a single `GF_TEXTEDITOR` object. This design minimizes global state and makes the editor easy to embed into existing projects.

## Features

* Full-screen terminal interface (MS-DOS EDIT style)
* Multiple document tabs
* Open, create, save and "Save As" support
* Insert and overwrite editing modes
* Text selection, copy, cut and paste
* Multi-level Undo
* Find, Find Next/Previous and Replace
* Automatic syntax highlighting for:

  * C
  * C++
  * BASIC
* UTF-8 aware editing (cursor movement, insertion and deletion)
* Status bar with live clock
* Configurable editor options
* Keyboard shortcuts for common editing operations

## Architecture

GF_TEXTEDITOR follows an object-based design while remaining 100% standard C.

The editor is represented by a single structure containing both its internal state and its public methods. A constructor initializes the object, the application interacts with it through its public API, and a destructor releases all allocated resources.

Typical usage:

```c
GF_TEXTEDITOR *editor = gf_texteditor_constructor();

gf_texteditor_open_file(editor, "example.c");

editor->run(editor);

gf_texteditor_destroy(editor);
```

Although C does not provide native object-oriented programming, this approach offers many of the same benefits:

* Encapsulation of all editor data
* Clean and reusable API
* Minimal global state
* Easy integration into larger applications
* Independent editor instances

## Internal Design

The editor manages:

* Multiple document buffers
* Clipboard
* Undo snapshots
* Syntax highlighting
* Search and replace
* Cursor and viewport management
* Background syntax highlighting thread
* Independent clock thread
* Dialog windows
* UTF-8 aware text manipulation

Each subsystem is isolated behind the public API, allowing the editor to be used as a reusable library rather than as a standalone program only.

## Purpose

GF_TEXTEDITOR was designed to demonstrate how a large and complex application can be implemented in pure C while adopting a modular, component-oriented architecture inspired by object-oriented principles, without relying on C++.
