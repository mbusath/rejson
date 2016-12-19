#include "json_path.h"

int _tokenizePath(const char *json, size_t len, SearchPath *path) {
    tokenizerState st = S_NULL;
    size_t offset = 0;
    char *pos = (char *)json;
    token tok;
    tok.s = pos;
    tok.len = 0;
    while (offset < len) {
        char c = *pos;
        switch (st) {
            // initial state
            case S_NULL: {
                switch (c) {
                    // . at the beginning means "root"
                    case '.':
                        tok.s++;
                        st = S_IDENT;
                        break;
                    // start of key/index specifier
                    case '[':
                        tok.s++;
                        st = S_BRACKET;
                        break;
                    default:
                        // only alpha allowed in the beginning
                        if (isalpha(c)) {
                            tok.len++;
                            st = S_IDENT;
                            break;
                        }

                        goto syntaxerror;
                }
            } break;

            // we're after a square bracket opening
            case S_BRACKET:  // [
                // quote after brackets means dict key
                if (c == '"') {
                    // skip to the beginnning of the key
                    tok.s++;
                    st = S_KEY;
                } else if (isdigit(c)) {
                    // digit after bracket means numeric index
                    tok.len++;
                    st = S_NUMBER;
                } else if ('-' == c) {
                    // this could be the beginning of a negative index
                    tok.len++;
                    st = S_MINUS;
                } else {
                    goto syntaxerror;
                }
                break;

            // we're after a dot
            case S_DOT:
                // start of ident token
                if (isalpha(c)) {
                    tok.len++;
                    st = S_IDENT;
                } else {
                    goto syntaxerror;
                }
                break;

            // we're within a number (array index)
            case S_NUMBER:
                if (isdigit(c)) {
                    tok.len++;
                    break;
                }
                if (c == ']') {
                    st = S_NULL;
                    tok.type = T_INDEX;
                    pos++;
                    offset++;
                    goto tokenend;
                }
                goto syntaxerror;

            // we're within an ident string
            case S_IDENT:
                // end of ident
                if (c == '.' || c == '[') {
                    st = c == '.' ? S_DOT : S_BRACKET;
                    tok.type = T_KEY;
                    pos++;
                    offset++;
                    goto tokenend;
                }
                // we only allow letters, numbers and underscores in identifiers
                if (!isalnum(c) && c != '_') {
                    goto syntaxerror;
                }
                // advance one
                tok.len++;
                break;

            // we're within a bracketed string key
            case S_KEY:
                // end of key
                if (c == '"') {
                    if (offset < len - 1 && *(pos + 1) == ']') {
                        tok.type = T_KEY;
                        pos += 2;
                        offset += 2;
                        st = S_NULL;
                        goto tokenend;
                    } else {
                        goto syntaxerror;
                    }
                }
                tok.len++;
                break;

            // we're within a negative index so we expect a digit now
            case S_MINUS:
                if (isdigit(c)) {
                    tok.len++;
                    st = S_NUMBER;
                } else {
                    goto syntaxerror;
                }
                break;
        }  // switch (st)
        offset++;
        pos++;
        
        // ident string must end if len reached
        if (S_IDENT == st && len == offset) {
            st = S_NULL;
            tok.type = T_KEY;
            goto tokenend;
        }
        continue;
    tokenend : {
        if (T_INDEX == tok.type) {
            // convert the string to int. we can't use atoi because it expects
            // NULL termintated strings
            int64_t num = 0;
            for (int i = !isdigit(tok.s[0]); i < tok.len; i++) {
                int digit = tok.s[i] - '0';
                num = num * 10 + digit;
            }
            if ('-' == tok.s[0]) num = -num;
            SearchPath_AppendIndex(path, num);
        } else if (T_KEY == tok.type) {
            if (1 == offset == len && '.' == c) {  // check for root
                SearchPath_AppendRoot(path);
            } else {
                SearchPath_AppendKey(path, tok.s, tok.len);
            }
        }
        tok.s = pos;
        tok.len = 0;
    }
    }  // while (offset < len)

    // these are the only legal states at the end of consuming the string
    if (st == S_NULL || st == S_IDENT) {
        return OBJ_OK;
    }

syntaxerror:
    return OBJ_ERR;
}

int ParseJSONPath(const char *json, size_t len, SearchPath *path) {
    return _tokenizePath(json, len, path);
}
