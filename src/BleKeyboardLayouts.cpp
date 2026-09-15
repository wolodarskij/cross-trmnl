#include "BleKeyboardLayouts.h"

#include <cstring>

// Tables are keyed by HID usage and list only what differs from the parent
// layout (see the header). Sources: the Windows / X11 layouts of the same
// name. AltGr levels are included where the layout has them; dead keys emit
// their spacing accent character instead of composing with the next letter.

namespace blelayout {

namespace {

using namespace hid;

// --- US (QWERTY): the root every other layout falls back to ----------------
constexpr Key kUs[] = {
    {A, u'a', u'A'},
    {B, u'b', u'B'},
    {C, u'c', u'C'},
    {D, u'd', u'D'},
    {E, u'e', u'E'},
    {F, u'f', u'F'},
    {G, u'g', u'G'},
    {H, u'h', u'H'},
    {I, u'i', u'I'},
    {J, u'j', u'J'},
    {K, u'k', u'K'},
    {L, u'l', u'L'},
    {M, u'm', u'M'},
    {N, u'n', u'N'},
    {O, u'o', u'O'},
    {P, u'p', u'P'},
    {Q, u'q', u'Q'},
    {R, u'r', u'R'},
    {S, u's', u'S'},
    {T, u't', u'T'},
    {U, u'u', u'U'},
    {V, u'v', u'V'},
    {W, u'w', u'W'},
    {X, u'x', u'X'},
    {Y, u'y', u'Y'},
    {Z, u'z', u'Z'},
    {N1, u'1', u'!'},
    {N2, u'2', u'@'},
    {N3, u'3', u'#'},
    {N4, u'4', u'$'},
    {N5, u'5', u'%'},
    {N6, u'6', u'^'},
    {N7, u'7', u'&'},
    {N8, u'8', u'*'},
    {N9, u'9', u'('},
    {N0, u'0', u')'},
    {SPACE, u' ', u' '},
    {MINUS, u'-', u'_'},
    {EQUAL, u'=', u'+'},
    {LBRACKET, u'[', u'{'},
    {RBRACKET, u']', u'}'},
    {BACKSLASH, u'\\', u'|'},
    {NONUS_HASH, u'\\', u'|'},
    {SEMICOLON, u';', u':'},
    {QUOTE, u'\'', u'"'},
    {GRAVE, u'`', u'~'},
    {COMMA, u',', u'<'},
    {DOT, u'.', u'>'},
    {SLASH, u'/', u'?'},
    {NONUS_BSLASH, u'\\', u'|'},
    // Keypad: always digits / operators (Num Lock state is not tracked).
    {KP_SLASH, u'/', u'/'},
    {KP_STAR, u'*', u'*'},
    {KP_MINUS, u'-', u'-'},
    {KP_PLUS, u'+', u'+'},
    {KP_1, u'1', u'1'},
    {KP_2, u'2', u'2'},
    {KP_3, u'3', u'3'},
    {KP_4, u'4', u'4'},
    {KP_5, u'5', u'5'},
    {KP_6, u'6', u'6'},
    {KP_7, u'7', u'7'},
    {KP_8, u'8', u'8'},
    {KP_9, u'9', u'9'},
    {KP_0, u'0', u'0'},
    {KP_DOT, u'.', u'.'},
    {KP_COMMA, u',', u','},
};

// --- English (UK) -----------------------------------------------------------
constexpr Key kUk[] = {
    {N2, u'2', u'"'},          {N3, u'3', u'£'},        {N4, u'4', u'$', u'€'},   {QUOTE, u'\'', u'@'},
    {GRAVE, u'`', u'¬', u'¦'}, {BACKSLASH, u'#', u'~'}, {NONUS_HASH, u'#', u'~'}, {NONUS_BSLASH, u'\\', u'|'},
};

// --- German (QWERTZ) --------------------------------------------------------
constexpr Key kDe[] = {
    {Y, u'z', u'Z'},
    {Z, u'y', u'Y'},
    {Q, u'q', u'Q', u'@'},
    {E, u'e', u'E', u'€'},
    {M, u'm', u'M', u'µ'},
    {N2, u'2', u'"', u'²'},
    {N3, u'3', u'§', u'³'},
    {N6, u'6', u'&'},
    {N7, u'7', u'/', u'{'},
    {N8, u'8', u'(', u'['},
    {N9, u'9', u')', u']'},
    {N0, u'0', u'=', u'}'},
    {MINUS, u'ß', u'?', u'\\'},
    {EQUAL, u'´', u'`'},
    {LBRACKET, u'ü', u'Ü'},
    {RBRACKET, u'+', u'*', u'~'},
    {BACKSLASH, u'#', u'\''},
    {NONUS_HASH, u'#', u'\''},
    {SEMICOLON, u'ö', u'Ö'},
    {QUOTE, u'ä', u'Ä'},
    {GRAVE, u'^', u'°'},
    {COMMA, u',', u';'},
    {DOT, u'.', u':'},
    {SLASH, u'-', u'_'},
    {NONUS_BSLASH, u'<', u'>', u'|'},
};

// --- French (AZERTY) --------------------------------------------------------
constexpr Key kFr[] = {
    {A, u'q', u'Q'},
    {Q, u'a', u'A'},
    {W, u'z', u'Z'},
    {Z, u'w', u'W'},
    {M, u',', u'?'},
    {SEMICOLON, u'm', u'M'},
    {E, u'e', u'E', u'€'},
    {N1, u'&', u'1'},
    {N2, u'é', u'2', u'~'},
    {N3, u'"', u'3', u'#'},
    {N4, u'\'', u'4', u'{'},
    {N5, u'(', u'5', u'['},
    {N6, u'-', u'6', u'|'},
    {N7, u'è', u'7', u'`'},
    {N8, u'_', u'8', u'\\'},
    {N9, u'ç', u'9', u'^'},
    {N0, u'à', u'0', u'@'},
    {MINUS, u')', u'°', u']'},
    {EQUAL, u'=', u'+', u'}'},
    {LBRACKET, u'^', u'¨'},
    {RBRACKET, u'$', u'£', u'¤'},
    {BACKSLASH, u'*', u'µ'},
    {NONUS_HASH, u'*', u'µ'},
    {QUOTE, u'ù', u'%'},
    {GRAVE, u'²', 0},
    {COMMA, u';', u'.'},
    {DOT, u':', u'/'},
    {SLASH, u'!', u'§'},
    {NONUS_BSLASH, u'<', u'>'},
};

// --- Spanish (Spain) --------------------------------------------------------
constexpr Key kEs[] = {
    {E, u'e', u'E', u'€'},
    {N1, u'1', u'!', u'|'},
    {N2, u'2', u'"', u'@'},
    {N3, u'3', u'·', u'#'},
    {N4, u'4', u'$', u'~'},
    {N6, u'6', u'&', u'¬'},
    {N7, u'7', u'/'},
    {N8, u'8', u'('},
    {N9, u'9', u')'},
    {N0, u'0', u'='},
    {MINUS, u'\'', u'?'},
    {EQUAL, u'¡', u'¿'},
    {LBRACKET, u'`', u'^', u'['},
    {RBRACKET, u'+', u'*', u']'},
    {BACKSLASH, u'ç', u'Ç', u'}'},
    {NONUS_HASH, u'ç', u'Ç', u'}'},
    {SEMICOLON, u'ñ', u'Ñ'},
    {QUOTE, u'´', u'¨', u'{'},
    {GRAVE, u'º', u'ª', u'\\'},
    {COMMA, u',', u';'},
    {DOT, u'.', u':'},
    {SLASH, u'-', u'_'},
    {NONUS_BSLASH, u'<', u'>'},
};

// --- Italian ----------------------------------------------------------------
constexpr Key kIt[] = {
    {E, u'e', u'E', u'€'},
    {N2, u'2', u'"'},
    {N3, u'3', u'£'},
    {N6, u'6', u'&'},
    {N7, u'7', u'/'},
    {N8, u'8', u'('},
    {N9, u'9', u')'},
    {N0, u'0', u'='},
    {MINUS, u'\'', u'?'},
    {EQUAL, u'ì', u'^'},
    {LBRACKET, u'è', u'é', u'['},
    {RBRACKET, u'+', u'*', u']'},
    {BACKSLASH, u'ù', u'§'},
    {NONUS_HASH, u'ù', u'§'},
    {SEMICOLON, u'ò', u'ç', u'@'},
    {QUOTE, u'à', u'°', u'#'},
    {GRAVE, u'\\', u'|'},
    {COMMA, u',', u';'},
    {DOT, u'.', u':'},
    {SLASH, u'-', u'_'},
    {NONUS_BSLASH, u'<', u'>'},
};

// --- Portuguese (Portugal) --------------------------------------------------
constexpr Key kPt[] = {
    {E, u'e', u'E', u'€'},  {N2, u'2', u'"', u'@'},     {N3, u'3', u'#', u'£'},   {N4, u'4', u'$', u'§'},
    {N6, u'6', u'&'},       {N7, u'7', u'/', u'{'},     {N8, u'8', u'(', u'['},   {N9, u'9', u')', u']'},
    {N0, u'0', u'=', u'}'}, {MINUS, u'\'', u'?'},       {EQUAL, u'«', u'»'},      {LBRACKET, u'+', u'*'},
    {RBRACKET, u'´', u'`'}, {BACKSLASH, u'~', u'^'},    {NONUS_HASH, u'~', u'^'}, {SEMICOLON, u'ç', u'Ç'},
    {QUOTE, u'º', u'ª'},    {GRAVE, u'\\', u'|'},       {COMMA, u',', u';'},      {DOT, u'.', u':'},
    {SLASH, u'-', u'_'},    {NONUS_BSLASH, u'<', u'>'},
};

// --- Portuguese (Brazil, ABNT2) ---------------------------------------------
constexpr Key kBr[] = {
    {N1, u'1', u'!', u'¹'},
    {N2, u'2', u'@', u'²'},
    {N3, u'3', u'#', u'³'},
    {N4, u'4', u'$', u'£'},
    {N5, u'5', u'%', u'¢'},
    {N6, u'6', u'¨', u'¬'},
    {N7, u'7', u'&'},
    {N8, u'8', u'*'},
    {N9, u'9', u'('},
    {N0, u'0', u')'},
    {EQUAL, u'=', u'+', u'§'},
    {LBRACKET, u'´', u'`'},
    {RBRACKET, u'[', u'{', u'ª'},
    {BACKSLASH, u']', u'}', u'º'},
    {NONUS_HASH, u']', u'}', u'º'},
    {SEMICOLON, u'ç', u'Ç'},
    {QUOTE, u'~', u'^'},
    {GRAVE, u'\'', u'"'},
    {SLASH, u';', u':'},
    {NONUS_BSLASH, u'\\', u'|'},
    {INTL1, u'/', u'?', u'°'},
};

// --- Swedish / Finnish ------------------------------------------------------
constexpr Key kSe[] = {
    {E, u'e', u'E', u'€'},    {M, u'm', u'M', u'µ'},     {N2, u'2', u'"', u'@'},
    {N3, u'3', u'#', u'£'},   {N4, u'4', u'¤', u'$'},    {N5, u'5', u'%', u'€'},
    {N6, u'6', u'&'},         {N7, u'7', u'/', u'{'},    {N8, u'8', u'(', u'['},
    {N9, u'9', u')', u']'},   {N0, u'0', u'=', u'}'},    {MINUS, u'+', u'?', u'\\'},
    {EQUAL, u'´', u'`'},      {LBRACKET, u'å', u'Å'},    {RBRACKET, u'¨', u'^', u'~'},
    {BACKSLASH, u'\'', u'*'}, {NONUS_HASH, u'\'', u'*'}, {SEMICOLON, u'ö', u'Ö'},
    {QUOTE, u'ä', u'Ä'},      {GRAVE, u'§', u'½'},       {COMMA, u',', u';'},
    {DOT, u'.', u':'},        {SLASH, u'-', u'_'},       {NONUS_BSLASH, u'<', u'>', u'|'},
};

// --- Danish (differences from Swedish) --------------------------------------
constexpr Key kDk[] = {
    {EQUAL, u'´', u'`', u'|'}, {SEMICOLON, u'æ', u'Æ'}, {QUOTE, u'ø', u'Ø'},
    {GRAVE, u'½', u'§'},       {MINUS, u'+', u'?'},     {NONUS_BSLASH, u'<', u'>', u'\\'},
};

// --- Russian (ЙЦУКЕН) -------------------------------------------------------
constexpr Key kRu[] = {
    {Q, u'й', u'Й'},           {W, u'ц', u'Ц'},
    {E, u'у', u'У'},           {R, u'к', u'К'},
    {T, u'е', u'Е'},           {Y, u'н', u'Н'},
    {U, u'г', u'Г'},           {I, u'ш', u'Ш'},
    {O, u'щ', u'Щ'},           {P, u'з', u'З'},
    {LBRACKET, u'х', u'Х'},    {RBRACKET, u'ъ', u'Ъ'},
    {A, u'ф', u'Ф'},           {S, u'ы', u'Ы'},
    {D, u'в', u'В'},           {F, u'а', u'А'},
    {G, u'п', u'П'},           {H, u'р', u'Р'},
    {J, u'о', u'О'},           {K, u'л', u'Л'},
    {L, u'д', u'Д'},           {SEMICOLON, u'ж', u'Ж'},
    {QUOTE, u'э', u'Э'},       {BACKSLASH, u'\\', u'/'},
    {NONUS_HASH, u'\\', u'/'}, {NONUS_BSLASH, u'\\', u'/'},
    {Z, u'я', u'Я'},           {X, u'ч', u'Ч'},
    {C, u'с', u'С'},           {V, u'м', u'М'},
    {B, u'и', u'И'},           {N, u'т', u'Т'},
    {M, u'ь', u'Ь'},           {COMMA, u'б', u'Б'},
    {DOT, u'ю', u'Ю'},         {SLASH, u'.', u','},
    {GRAVE, u'ё', u'Ё'},       {N2, u'2', u'"'},
    {N3, u'3', u'№'},          {N4, u'4', u';'},
    {N6, u'6', u':'},          {N7, u'7', u'?'},
};

// --- Ukrainian (differences from Russian) -----------------------------------
constexpr Key kUa[] = {
    {S, u'і', u'І'},         {RBRACKET, u'ї', u'Ї'},   {QUOTE, u'є', u'Є'},
    {BACKSLASH, u'ґ', u'Ґ'}, {NONUS_HASH, u'ґ', u'Ґ'}, {GRAVE, u'\'', u'₴'},
};

// --- Polish (programmers): US + AltGr diacritics ----------------------------
constexpr Key kPl[] = {
    {A, u'a', u'A', u'ą', u'Ą'}, {C, u'c', u'C', u'ć', u'Ć'}, {E, u'e', u'E', u'ę', u'Ę'},
    {L, u'l', u'L', u'ł', u'Ł'}, {N, u'n', u'N', u'ń', u'Ń'}, {O, u'o', u'O', u'ó', u'Ó'},
    {S, u's', u'S', u'ś', u'Ś'}, {X, u'x', u'X', u'ź', u'Ź'}, {Z, u'z', u'Z', u'ż', u'Ż'},
};

// --- Czech (QWERTZ) ---------------------------------------------------------
constexpr Key kCz[] = {
    {Y, u'z', u'Z'},
    {Z, u'y', u'Y'},
    {Q, u'q', u'Q', u'\\'},
    {W, u'w', u'W', u'|'},
    {E, u'e', u'E', u'€'},
    {F, u'f', u'F', u'['},
    {G, u'g', u'G', u']'},
    {B, u'b', u'B', u'{'},
    {N, u'n', u'N', u'}'},
    {V, u'v', u'V', u'@'},
    {X, u'x', u'X', u'#'},
    {C, u'c', u'C', u'&'},
    {M, u'm', u'M', u'µ'},
    {N1, u'+', u'1', u'~'},
    {N2, u'ě', u'2', u'ˇ'},
    {N3, u'š', u'3', u'^'},
    {N4, u'č', u'4', u'˘'},
    {N5, u'ř', u'5', u'°'},
    {N6, u'ž', u'6', u'˛'},
    {N7, u'ý', u'7', u'`'},
    {N8, u'á', u'8', u'˙'},
    {N9, u'í', u'9', u'´'},
    {N0, u'é', u'0', u'˝'},
    {MINUS, u'=', u'%'},
    {EQUAL, u'´', u'ˇ'},
    {LBRACKET, u'ú', u'/', u'['},
    {RBRACKET, u')', u'(', u']'},
    {BACKSLASH, u'¨', u'\'', u'\\'},
    {NONUS_HASH, u'¨', u'\'', u'\\'},
    {SEMICOLON, u'ů', u'"'},
    {QUOTE, u'§', u'!'},
    {GRAVE, u';', u'°'},
    {COMMA, u',', u'?', u'<'},
    {DOT, u'.', u':', u'>'},
    {SLASH, u'-', u'_', u'*'},
    {NONUS_BSLASH, u'\\', u'|'},
};

// --- Turkish (Q) ------------------------------------------------------------
constexpr Key kTr[] = {
    {I, u'ı', u'I'},
    {Q, u'q', u'Q', u'@'},
    {E, u'e', u'E', u'€'},
    {N2, u'2', u'\''},
    {N3, u'3', u'^', u'#'},
    {N4, u'4', u'+', u'$'},
    {N6, u'6', u'&'},
    {N7, u'7', u'/', u'{'},
    {N8, u'8', u'(', u'['},
    {N9, u'9', u')', u']'},
    {N0, u'0', u'=', u'}'},
    {MINUS, u'*', u'?', u'\\'},
    {EQUAL, u'-', u'_'},
    {LBRACKET, u'ğ', u'Ğ', u'¨'},
    {RBRACKET, u'ü', u'Ü', u'~'},
    {BACKSLASH, u',', u';', u'`'},
    {NONUS_HASH, u',', u';', u'`'},
    {SEMICOLON, u'ş', u'Ş', u'´'},
    {QUOTE, u'i', u'İ'},
    {GRAVE, u'"', u'é', u'<'},
    {COMMA, u'ö', u'Ö'},
    {DOT, u'ç', u'Ç'},
    {SLASH, u'.', u':'},
    {NONUS_BSLASH, u'<', u'>', u'|'},
};

template <size_t N>
constexpr uint16_t countOf(const Key (&)[N]) {
  return static_cast<uint16_t>(N);
}

// One Layout object per table; a parent points at another Layout object so a
// derivative (Danish from Swedish, Ukrainian from Russian) lists only its own
// differences. nullptr means "differs from US".
constexpr Layout kUsLayout = {"us", StrId::STR_BT_LAYOUT_US, kUs, countOf(kUs), nullptr};
constexpr Layout kUkLayout = {"uk", StrId::STR_BT_LAYOUT_UK, kUk, countOf(kUk), nullptr};
constexpr Layout kDeLayout = {"de", StrId::STR_BT_LAYOUT_DE, kDe, countOf(kDe), nullptr};
constexpr Layout kFrLayout = {"fr", StrId::STR_BT_LAYOUT_FR, kFr, countOf(kFr), nullptr};
constexpr Layout kEsLayout = {"es", StrId::STR_BT_LAYOUT_ES, kEs, countOf(kEs), nullptr};
constexpr Layout kItLayout = {"it", StrId::STR_BT_LAYOUT_IT, kIt, countOf(kIt), nullptr};
constexpr Layout kPtLayout = {"pt", StrId::STR_BT_LAYOUT_PT, kPt, countOf(kPt), nullptr};
constexpr Layout kBrLayout = {"br", StrId::STR_BT_LAYOUT_BR, kBr, countOf(kBr), nullptr};
constexpr Layout kSeLayout = {"se", StrId::STR_BT_LAYOUT_SE, kSe, countOf(kSe), nullptr};
constexpr Layout kDkLayout = {"dk", StrId::STR_BT_LAYOUT_DK, kDk, countOf(kDk), &kSeLayout};
constexpr Layout kRuLayout = {"ru", StrId::STR_BT_LAYOUT_RU, kRu, countOf(kRu), nullptr};
constexpr Layout kUaLayout = {"ua", StrId::STR_BT_LAYOUT_UA, kUa, countOf(kUa), &kRuLayout};
constexpr Layout kPlLayout = {"pl", StrId::STR_BT_LAYOUT_PL, kPl, countOf(kPl), nullptr};
constexpr Layout kCzLayout = {"cz", StrId::STR_BT_LAYOUT_CZ, kCz, countOf(kCz), nullptr};
constexpr Layout kTrLayout = {"tr", StrId::STR_BT_LAYOUT_TR, kTr, countOf(kTr), nullptr};

// Registry, in picker order. kLayouts[0] must stay US: it is the implicit root
// of every parent chain.
constexpr const Layout* kLayouts[] = {
    &kUsLayout, &kUkLayout, &kDeLayout, &kFrLayout, &kEsLayout, &kItLayout, &kPtLayout, &kBrLayout,
    &kSeLayout, &kDkLayout, &kRuLayout, &kUaLayout, &kPlLayout, &kCzLayout, &kTrLayout,
};
constexpr uint8_t kLayoutCount = static_cast<uint8_t>(sizeof(kLayouts) / sizeof(kLayouts[0]));

const Key* findInTable(const Layout& layout, uint8_t usage) {
  for (uint16_t i = 0; i < layout.keyCount; i++) {
    if (layout.keys[i].usage == usage) return &layout.keys[i];
  }
  return nullptr;
}

// Walk layout -> parent -> ... -> US. Bounded so a mis-wired parent cycle in a
// new table degrades to "no character" instead of hanging the main loop.
const Key* findKey(const Layout& layout, uint8_t usage) {
  const Layout* l = &layout;
  for (int depth = 0; l != nullptr && depth < 8; depth++) {
    if (const Key* k = findInTable(*l, usage)) return k;
    l = l->parent;
  }
  return &layout == kLayouts[0] ? nullptr : findInTable(*kLayouts[0], usage);
}

// True when (base, shift) is a lower/upper-case pair, i.e. a key Caps Lock
// should affect. Range checks cover the scripts the shipped layouts use.
bool isCasePair(char16_t base, char16_t shift) {
  if (shift == 0 || shift == base) return false;
  if (base >= u'a' && base <= u'z') return shift == base - 0x20;
  if (base >= 0x00E0 && base <= 0x00FE && base != 0x00F7) return shift == base - 0x20;  // Latin-1
  if (base >= 0x0100 && base <= 0x017F) return shift == base - 1;                       // Latin Extended-A
  if (base >= 0x03B1 && base <= 0x03C9) return shift == base - 0x20;                    // Greek
  if (base >= 0x0430 && base <= 0x044F) return shift == base - 0x20;                    // Cyrillic
  if (base >= 0x0450 && base <= 0x045F) return shift == base - 0x50;                    // Cyrillic (ё, і, ї, є...)
  if (base >= 0x0460 && base <= 0x04FF) return shift == base - 1;                       // Cyrillic extended (ґ...)
  return false;
}

}  // namespace

uint8_t count() { return kLayoutCount; }

const Layout& at(uint8_t index) { return *kLayouts[index < kLayoutCount ? index : 0]; }

const Layout* find(const char* id) {
  if (!id || !*id) return nullptr;
  for (uint8_t i = 0; i < kLayoutCount; i++) {
    if (strcmp(kLayouts[i]->id, id) == 0) return kLayouts[i];
  }
  return nullptr;
}

uint8_t indexOf(const Layout& layout) {
  for (uint8_t i = 0; i < kLayoutCount; i++) {
    if (kLayouts[i] == &layout) return i;
  }
  return 0;
}

const Layout& fallback() { return *kLayouts[0]; }

char16_t lookup(const Layout& layout, uint8_t usage, uint8_t mods, bool capsLock) {
  const Key* k = findKey(layout, usage);
  if (!k) return 0;

  const bool shift = (mods & (mod::LSHIFT | mod::RSHIFT)) != 0;
  const bool altgr = (mods & mod::RALT) != 0 || (mods & (mod::LCTRL | mod::LALT)) == (mod::LCTRL | mod::LALT);
  if (altgr) return shift ? k->shiftAltgr : k->altgr;

  bool upper = shift;
  if (capsLock && isCasePair(k->base, k->shift)) upper = !upper;
  return upper ? k->shift : k->base;
}

}  // namespace blelayout
