# KodoTerm QML Example

This example demonstrates how to implement a native QML terminal item using `QQuickPaintedItem`.

## Implementation Details

Unlike the "embedding" approach, this example implements a custom QQuickItem (`KodoQuickTerm`) that performs its own painting using `QPainter`.

1.  **KodoQuickTerm**: Inherits from `QQuickPaintedItem`.
2.  **Rendering**: Renders the terminal content (from `libvterm`) directly into a QImage backbuffer, which is then drawn to the QML scene.
3.  **Input**: Handles key and mouse events directly within the QML item and forwards them to the PTY/vterm.
4.  **Integration**: Fully integrated into the QML scene graph, supporting transparency, layering, and QML transforms.

## Features

*   Full terminal emulation (via `libvterm`).
*   Native QML focus and input handling.
*   Customizable via QML properties (program, scroll position).
*   No "airgap" issues (z-ordering works as expected).