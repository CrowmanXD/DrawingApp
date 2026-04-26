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

class DeleteLayerCommand : public UndoCommand {
public:
    DeleteLayerCommand(int startIndex, std::vector<std::unique_ptr<Layer>> savedLayers, int oldActiveIndex);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_startIndex;
    int m_count;
    int m_oldActiveIndex;
    std::vector<std::unique_ptr<Layer>> m_savedLayers;
};

class RenameLayerCommand : public UndoCommand {
public:
    RenameLayerCommand(int layerIndex, const std::string& oldName, const std::string& newName);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    std::string m_oldName;
    std::string m_newName;
};

class VisibilityChangeCommand : public UndoCommand {
public:
    VisibilityChangeCommand(int layerIndex, bool oldState, bool newState);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    bool m_oldState;
    bool m_newState;
};

class ClipLayerCommand : public UndoCommand {
public:
    ClipLayerCommand(int layerIndex, bool oldState, bool newState);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_layerIndex;
    bool m_oldState;
    bool m_newState;
};

class MergeLayerCommand : public UndoCommand {
public:
    MergeLayerCommand(int startIndex, int count, std::vector<std::unique_ptr<Layer>> originalLayers, std::unique_ptr<Layer> mergedLayer);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    int m_startIndex;
    int m_count;
    std::vector<std::unique_ptr<Layer>> m_originalLayers;
    std::unique_ptr<Layer> m_mergedLayer;
};

class BatchCommand : public UndoCommand {
public:
    BatchCommand() = default;
    void addCommand(std::unique_ptr<UndoCommand> cmd);
    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;
    bool isEmpty() const;

private:
    std::vector<std::unique_ptr<UndoCommand>> m_commands;
};