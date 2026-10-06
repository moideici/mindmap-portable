#pragma once
// Undo/redo history with compressed snapshots (Windows Compression API, XPRESS+Huffman).
// JSON snapshots shrink about tenfold, so far more steps fit in a much smaller memory budget.
#include <windows.h>
#include <compressapi.h>
#include <deque>
#include <stdexcept>
#include <string>

class History {
    struct Entry { std::string data; size_t raw = 0; bool packed = false; };
    std::deque<Entry> undo_steps, redo_steps;
    COMPRESSOR_HANDLE compressor = nullptr;
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    size_t stored = 0;

    Entry pack(const std::string& text) {
        Entry entry{text, text.size(), false};
        SIZE_T needed = 0;
        if (compressor && !text.empty()) {
            Compress(compressor, const_cast<char*>(text.data()), text.size(), nullptr, 0, &needed);
            std::string packed(needed, '\0');
            SIZE_T written = 0;
            if (needed && Compress(compressor, const_cast<char*>(text.data()), text.size(), packed.data(), packed.size(), &written) && written < text.size()) {
                packed.resize(written);
                entry = {std::move(packed), text.size(), true};
            }
        }
        return entry;
    }
    std::string unpack(const Entry& entry) const {
        if (!entry.packed) return entry.data;
        std::string text(entry.raw, '\0');
        SIZE_T written = 0;
        if (!Decompress(decompressor, const_cast<char*>(entry.data.data()), entry.data.size(), text.data(), text.size(), &written) || written != entry.raw)
            throw std::runtime_error("Historique illisible");
        return text;
    }
    void push(std::deque<Entry>& steps, const std::string& text) {
        steps.push_back(pack(text));
        stored += steps.back().data.size();
    }
    std::string pop(std::deque<Entry>& steps) {
        auto text = unpack(steps.back());
        stored -= steps.back().data.size();
        steps.pop_back();
        return text;
    }
    void trim() {
        while (undo_steps.size() > max_steps || (stored > max_bytes && undo_steps.size() > 1)) {
            stored -= undo_steps.front().data.size();
            undo_steps.pop_front();
        }
    }
public:
    size_t max_steps = 500, max_bytes = 16 * 1024 * 1024;
    History() {
        if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) compressor = nullptr;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor)) decompressor = nullptr;
        if (!decompressor && compressor) { CloseCompressor(compressor); compressor = nullptr; }
    }
    ~History() {
        if (compressor) CloseCompressor(compressor);
        if (decompressor) CloseDecompressor(decompressor);
    }
    History(const History&) = delete;
    History& operator=(const History&) = delete;

    void remember(const std::string& before) {
        if (!undo_steps.empty() && undo_steps.back().raw == before.size() && unpack(undo_steps.back()) == before) return;
        push(undo_steps, before);
        for (const auto& entry : redo_steps) stored -= entry.data.size();
        redo_steps.clear();
        trim();
    }
    bool can_undo() const { return !undo_steps.empty(); }
    bool can_redo() const { return !redo_steps.empty(); }
    // Returns the snapshot to restore and keeps the current one on the opposite stack.
    std::string undo(const std::string& current) { auto text = pop(undo_steps); push(redo_steps, current); return text; }
    std::string redo(const std::string& current) { auto text = pop(redo_steps); push(undo_steps, current); trim(); return text; }
    void clear() { undo_steps.clear(); redo_steps.clear(); stored = 0; }
    size_t steps() const { return undo_steps.size(); }
    size_t bytes() const { return stored; }
    size_t raw_bytes() const {
        size_t total = 0;
        for (const auto* steps : {&undo_steps, &redo_steps}) for (const auto& entry : *steps) total += entry.raw;
        return total;
    }
    bool compressed() const { return compressor != nullptr; }
};
