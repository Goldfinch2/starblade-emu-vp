// state.h: a save state's bytes, written and read by the same code. Each part
// of the machine has a state(State &) that passes its members through it, in
// the same order both ways.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

class State {
public:
    explicit State(bool saving) : saving_(saving) {}
    State(std::vector<uint8_t> data) : data_(std::move(data)), saving_(false) {}

    bool saving() const { return saving_; }
    bool ok() const { return ok_; }
    std::vector<uint8_t> &data() { return data_; }

    void bytes(void *p, size_t n)
    {
        if (saving_) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            data_.insert(data_.end(), b, b + n);
        } else if (ok_ && pos_ + n <= data_.size()) {
            memcpy(p, data_.data() + pos_, n);
            pos_ += n;
        } else {
            ok_ = false;
        }
    }
    template <class T> void pod(T &v) { bytes(&v, sizeof v); }
    // A vector: its size, then its elements. Loading checks the size is the
    // one it has now (every vector here is fixed by the ROM set) unless `any`.
    template <class T> void vec(std::vector<T> &v, bool any = false)
    {
        uint32_t n = (uint32_t)v.size();
        pod(n);
        if (!saving_) {
            if (!ok_ || (!any && n != v.size())) { ok_ = false; return; }
            v.resize(n);
        }
        if (n) bytes(v.data(), n * sizeof(T));
    }
    bool atEnd() const { return pos_ == data_.size(); }

private:
    std::vector<uint8_t> data_;
    size_t pos_ = 0;
    bool saving_;
    bool ok_ = true;
};
