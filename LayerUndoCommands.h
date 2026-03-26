#pragma once
#include "UndoCommand.h"
#include "Canvas.h"
#include <memory>

class AddLayerCommand : public UndoCommand {
public:
    AddLayerCommand(int layerIndex);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    std::unique_ptr<Layer> m_savedLayer; // Holds the layer memory while it is "undone"
};

class MoveLayerCommand : public UndoCommand {
public:
    MoveLayerCommand(int fromIndex, int toIndex);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_fromIndex;
    int m_toIndex;
};

class ReparentLayerCommand : public UndoCommand {
public:
    ReparentLayerCommand(int oldIndex, int newIndex, int oldDepth, int newDepth);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_oldIndex;
    int m_newIndex;
    int m_oldDepth;
    int m_newDepth;
};

class OpacityChangeCommand : public UndoCommand {
public:
    OpacityChangeCommand(int layerIndex, float oldOpacity, float newOpacity);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    float m_oldOpacity;
    float m_newOpacity;
};

class BlendModeChangeCommand : public UndoCommand {
public:
    BlendModeChangeCommand(int layerIndex, int oldMode, int newMode);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    int m_oldMode;
    int m_newMode;
};