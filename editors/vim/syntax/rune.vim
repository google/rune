" Vim syntax file
" Language: Rune
" Maintainer: Aiden Hall (aidenhall@google.com)

" make sure we don't bonk heads with another syntax
if exists("b:current_syntax")
  finish
endif

" this is roughtly the same as python, not sure if that's technically correct but it's a
" start
syn match runeClass '[a-zA-Z_]' display contained
syn match runeClass '[a-zA-Z_][a-zA-Z0-9_]*' display contained
syn match runeFunc '[a-zA-Z_]' display contained
syn match runeFunc '[a-zA-Z_][a-zA-Z0-9_]*' display contained

" Ints
syn match runeIntType '[iu]\(8\|16\|32\|64\)' display
syn match runeFloatType 'f\(8\|16\|32\|64\)' display
syn match runeInt '\d\+' nextgroup=runeIntType display
syn match runeFloat '\d\+\.\d\+' nextgroup=runeFloatType display


" Keywords organized in the way they should be
syn keyword runeTypeKeywords bool string
syn keyword runeDebugKeywords debug
syn keyword runeThrowKeywords throw
syn keyword runeBoolean true false
syn keyword runeBuiltinFunc arrayof assert isnull mod null ref reveal typeof unref widthof
syn keyword runeConditional else if case default switch
syn keyword runeRepeat do for in while
syn keyword runeImport as import importlib importrpc use
syn keyword runeStatements println print return yield
syn keyword runeQualifierKeywords const export exportlib extern final secret signed unsigned var
syn keyword runeDeclKeywords enum generate generator iterator operator rpc struct message unittest
syn keyword runeRelationKeywords relation appendcode prependcode cascade

" Declaration Keywords
syn keyword runeFuncKeyword func nextgroup=runeFunc skipwhite
syn keyword runeClassKeyword class nextgroup=runeClass skipwhite

" Actual language syntax
syn region runeDescBlock start="{" end="}" fold transparent
syn region runeString start='"' end='"'


" Syntax highlighting
hi link runeDeclKeywords Function
hi link runeStatements Statement
hi link runeBuiltinFunc Special
hi link runeRepeat Repeat
hi link runeImport Include
hi link runeClass Structure
hi link runeClassKeyword Statement
hi link runeFunc Function
hi link runeFuncKeyword Statement
hi link runeBoolean Boolean
hi link runeString String
hi link runeInt Number
hi link runeFloat Float
hi link runeIntType Type
hi link runeFloatType Type
hi link runeTypeKeywords Type
hi link runeDebugKeyword PreProc
hi link runeThrowKeyword Special
hi link runeConditional Conditional
hi link runeQualifierKeywords Exception
hi link runeDeclKeywords Statement
hi link runeRelationKeywords Special

let b:current_syntax = "rune"
