#ifndef FixedString_h
#define FixedString_h

#include <cstring>
#include <string>
#include <WString.h>

template <size_t N>
class FixedString {
private:
    char m_data[N];

public:
    FixedString() {
        m_data[0] = '\0';
    }

    FixedString(const char* s) {
        set(s);
    }

    FixedString(const std::string& s) {
        set(s.c_str());
    }

    FixedString(const String& s) {
        set(s.c_str());
    }

    template <size_t M>
    FixedString(const FixedString<M>& o) {
        set(o.c_str());
    }

    FixedString& operator=(const char* s) {
        set(s);
        return *this;
    }

    FixedString& operator=(const std::string& s) {
        set(s.c_str());
        return *this;
    }

    FixedString& operator=(const String& s) {
        set(s.c_str());
        return *this;
    }

    template <size_t M>
    FixedString& operator=(const FixedString<M>& o) {
        set(o.c_str());
        return *this;
    }

    void set(const char* s) {
        if (s) {
            std::strncpy(m_data, s, N - 1);
            m_data[N - 1] = '\0';
        } else {
            m_data[0] = '\0';
        }
    }

    void clear() {
        m_data[0] = '\0';
    }

    bool empty() const {
        return m_data[0] == '\0';
    }

    size_t size() const {
        return std::strlen(m_data);
    }

    size_t length() const {
        return std::strlen(m_data);
    }

    static constexpr size_t capacity() {
        return N - 1;
    }

    const char* c_str() const {
        return m_data;
    }

    char* data() {
        return m_data;
    }

    const char* data() const {
        return m_data;
    }

    char operator[](size_t idx) const {
        return m_data[idx];
    }

    char& operator[](size_t idx) {
        return m_data[idx];
    }

    char operator[](int idx) const {
        return m_data[idx];
    }

    char& operator[](int idx) {
        return m_data[idx];
    }

    operator const char*() const {
        return m_data;
    }

    operator std::string() const {
        return std::string(m_data);
    }

    operator String() const {
        return String(m_data);
    }

    bool operator==(const char* s) const {
        return std::strcmp(m_data, s ? s : "") == 0;
    }

    bool operator!=(const char* s) const {
        return !(*this == s);
    }

    bool operator==(const std::string& s) const {
        return std::strcmp(m_data, s.c_str()) == 0;
    }

    bool operator!=(const std::string& s) const {
        return !(*this == s);
    }

    bool operator==(const String& s) const {
        return std::strcmp(m_data, s.c_str()) == 0;
    }

    bool operator!=(const String& s) const {
        return !(*this == s);
    }

    template <size_t M>
    bool operator==(const FixedString<M>& o) const {
        return std::strcmp(m_data, o.c_str()) == 0;
    }

    template <size_t M>
    bool operator!=(const FixedString<M>& o) const {
        return !(*this == o);
    }

    bool operator<(const FixedString& o) const {
        return std::strcmp(m_data, o.m_data) < 0;
    }
};

template <size_t N>
inline bool operator==(const char* s, const FixedString<N>& f) {
    return f == s;
}

template <size_t N>
inline bool operator!=(const char* s, const FixedString<N>& f) {
    return f != s;
}

template <size_t N>
inline bool operator==(const std::string& s, const FixedString<N>& f) {
    return f == s;
}

template <size_t N>
inline bool operator!=(const std::string& s, const FixedString<N>& f) {
    return f != s;
}

template <size_t N>
inline bool operator==(const String& s, const FixedString<N>& f) {
    return f == s;
}

template <size_t N>
inline bool operator!=(const String& s, const FixedString<N>& f) {
    return f != s;
}

template <size_t N>
inline std::string operator+(const FixedString<N>& a, const char* b) {
    return std::string(a.c_str()) + (b ? b : "");
}

template <size_t N>
inline std::string operator+(const char* a, const FixedString<N>& b) {
    return std::string(a ? a : "") + b.c_str();
}

template <size_t N>
inline std::string operator+(const FixedString<N>& a, const std::string& b) {
    return std::string(a.c_str()) + b;
}

template <size_t N>
inline std::string operator+(const std::string& a, const FixedString<N>& b) {
    return a + b.c_str();
}

template <size_t N, size_t M>
inline std::string operator+(const FixedString<N>& a, const FixedString<M>& b) {
    return std::string(a.c_str()) + b.c_str();
}

#endif
