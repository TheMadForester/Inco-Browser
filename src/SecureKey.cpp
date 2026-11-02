#include "SecureKey.hpp"

#include <cstring>

#include <openssl/crypto.h>
#include <sys/mman.h>
#include <unistd.h>

SecureKey::SecureKey()
{
    const long page = ::sysconf(_SC_PAGESIZE);
    m_pageSize = page > 0 ? std::size_t(page) : 4096;
    void* p = ::mmap(nullptr, m_pageSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return;
    m_page = static_cast<unsigned char*>(p);
    m_locked = ::mlock(m_page, m_pageSize) == 0;
#ifdef MADV_DONTDUMP
    ::madvise(m_page, m_pageSize, MADV_DONTDUMP);
#endif
#ifdef MADV_WIPEONFORK
    ::madvise(m_page, m_pageSize, MADV_WIPEONFORK);
#endif
}

SecureKey::~SecureKey()
{
    if (!m_page)
        return;
    OPENSSL_cleanse(m_page, m_pageSize);
    if (m_locked)
        ::munlock(m_page, m_pageSize);
    ::munmap(m_page, m_pageSize);
}

bool SecureKey::assign(const unsigned char* bytes, std::size_t n)
{
    unsigned char* dst = resize(n);
    if (!dst)
        return false;
    if (n)
        std::memcpy(dst, bytes, n);
    return true;
}

unsigned char* SecureKey::resize(std::size_t n)
{
    clear();
    if (!m_page || n > kCapacity)
        return nullptr;
    m_len = n;
    return m_page;
}

void SecureKey::clear()
{
    if (m_page)
        OPENSSL_cleanse(m_page, kCapacity);
    m_len = 0;
}
