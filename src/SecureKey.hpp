#pragma once

#include <cstddef>

// Key bytes on their own page: locked in RAM (never swapped), left out of
// core dumps, and wiped with OPENSSL_cleanse on clear() and destruction.
// Holds at most kCapacity bytes (one AES-256 key or a few of them).
class SecureKey
{
public:
    static constexpr std::size_t kCapacity = 64;

    SecureKey();
    ~SecureKey();
    SecureKey(const SecureKey&) = delete;
    SecureKey& operator=(const SecureKey&) = delete;

    // Copies n bytes in. Returns false if n is too large or no page exists.
    bool assign(const unsigned char* bytes, std::size_t n);
    bool assign(const SecureKey& other) { return assign(other.data(), other.size()); }
    // Makes room for n bytes and returns the buffer to write into (or null).
    unsigned char* resize(std::size_t n);
    void clear();

    const unsigned char* data() const { return m_page; }
    unsigned char* data() { return m_page; }
    std::size_t size() const { return m_len; }
    bool isEmpty() const { return m_len == 0; }
    // True when mlock succeeded (RLIMIT_MEMLOCK may forbid it; still usable).
    bool isLocked() const { return m_locked; }

private:
    unsigned char* m_page = nullptr;
    std::size_t m_pageSize = 0;
    std::size_t m_len = 0;
    bool m_locked = false;
};
