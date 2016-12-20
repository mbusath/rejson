# Module commands

## General commands

JSON.SET <key> <path> <json>
JSON.GET <key> [INDENT <indentation-string>] [NEWLINE <line-break-string>] [SPACE <space-string>] [path ...]
JSON.DEL <key> <path>
JSON.TYPE key <path>
JSON.MGET <key> [key ...] <path>

## Number operations

JSON.NUMINCRBY <key> <path> <number>  
JSON.NUMMULTBY <key> <path> <number>

## String operations

JSON.STRLEN <key> <path>
JSON.STRAPPEND <key> <path>

## Array operations

JSON.ARRLEN <key> <path>
JSON.ARRAPPEND <key> <path> <json> [json ...]
JSON.ARRINSERT <key> <path> <index> <json> [json ...]
JSON.ARRINDEX <key> <path> <json-scalar> [start] [stop]
JSON.ARRTRIM <key> <path> <start> <stop>

## Object operations

JSON.OBJLEN <key> <path>
JSON.OBJKEYS <key> <path>

## Others
JSON.RESP <key>
JSON.FORGET is an alias to JSON.DEL
