#include "scripting.h"
#include "fs.h"
#include "utils.h"
#include "nand.h"
#include "gamecart.h"
#include "bootfirm.h"
#include "qrcodegen.h"
#include "game.h"
#include "power.h"
#include "unittype.h"
#include "region.h"
#include "rtc.h"
#include "sha.h"
#include "hid.h"
#include "ui.h"
#include "swkbd.h"
#include "png.h"
#include "ips.h"
#include "bps.h"
#include "pxi.h"


#define _MAX_ARGS       4
#define _ARG_MAX_LEN    512
#define _VAR_CNT_LEN    256
#define _VAR_NAME_LEN   32
#define _VAR_MAX_BUFF   256
#define _ERR_STR_LEN    256

#define _CHOICE_STR_LEN 32
#define _CHOICE_MAX_N   12

#define _CMD_NOT        "not"
#define _CMD_IF         "if"
#define _CMD_ELIF       "elif"
#define _CMD_ELSE       "else"
#define _CMD_END        "end"
#define _CMD_FOR        "for"
#define _CMD_NEXT       "next"

#define _ARG_TRUE       "TRUE"
#define _ARG_FALSE      "FALSE"
#define _VAR_FORPATH    "FORPATH"

#define _SKIP_BLOCK     1
#define _SKIP_TILL_END  2
#define _SKIP_TO_NEXT   3
#define _SKIP_TO_FOR    4

#define _MAX_FOR_DEPTH  16

// macros for textviewer
#define TV_VPAD         1 // vertical padding per line (above / below)
#define TV_HPAD         0 // horizontal padding per line (left)
#define TV_LNOS         4 // # of digits in line numbers (0 to disable)

#define TV_NLIN_DISP    (SCREEN_HEIGHT / (FONT_HEIGHT_EXT + (2*TV_VPAD)))
#define TV_LLEN_DISP    (((SCREEN_WIDTH_TOP - (2*TV_HPAD)) / FONT_WIDTH_EXT) - (TV_LNOS + 1))

// some useful macros
#define IS_WHITESPACE(c)    ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
#define MATCH_STR(s,l,c)    ((l == strlen(c)) && (strncmp(s, c, l) == 0))
#define _FLG(c)             ((c >= 'a') ? (1 << (c - 'a')) : ((c >= '0') ? (1 << (26 + c - '0')) : 0))

#define IS_CTRLFLOW_CMD(id) ((id == CMD_ID_IF) || (id == CMD_ID_ELIF) || (id == CMD_ID_ELSE) || (id == CMD_ID_END) || \
    (id == CMD_ID_GOTO) || (id == CMD_ID_LABELSEL) || \
    (id == CMD_ID_FOR) || (id == CMD_ID_NEXT))

static u32 script_color_active = 0;
static u32 script_color_comment = 0;
static u32 script_color_code = 0;

static inline u32 line_len(const char* text, u32 len, u32 ww, const char* line, char** eol) {
    u32 last = ((text + len) - line);
    u32 llen = 0;
    char* lf = NULL;
    char* spc = NULL;

    if (line >= (text + len))
        return 0; // early exit

    // search line feeds, spaces (only relevant for wordwrapped)
    for (llen = 0; !ww || (llen < ww); llen++) {
        if (ww && (line[llen] == ' ')) spc = (char*) (line + llen);
        if (!line[llen] || (line[llen] == '\n') || (llen >= last)) {
            lf = (char*) (line + llen);
            break;
        }
    }

    // line feed found, truncate trailing "empty" chars
    // for wordwrapped, stop line after last space (if any)
    if (lf) for (; (llen > 0) && (line[llen-1] <= ' '); llen--);
    else if (ww && spc) llen = (spc - line) + 1;

    // signal eol if required
    if (eol) *eol = lf;
    return llen;
}

static inline char* line_seek(const char* text, u32 len, u32 ww, const char* line, int add) {
    // safety checks /
    if (line < text) return NULL;
    if ((line >= (text + len)) && (add >= 0)) return (char*) line;

    if (!ww) { // non wordwrapped mode
        char* lf = ((char*) line - 1);

        // ensure we are at the start of the line
        while ((lf > text) && (*lf != '\n')) lf--;

        // handle backwards search
        for (; (add < 0) && (lf >= text); add++)
            for (lf--; (lf >= text) && (*lf != '\n'); lf--);

        // handle forwards search
        for (; (add > 0) && (lf < text + len); add--)
            for (lf++; (lf < text + len) && (*lf != '\n'); lf++);

        return lf + 1;
    } else { // wordwrapped mode
        char* l0 = (char*) line;

        // handle forwards wordwrapped search
        for (; (add > 0) && (l0 < text + len); add--) {
            char* eol = NULL;
            u32 llenww = line_len(text, len, ww, l0, &eol);
            if (eol || !llenww) l0 = line_seek(text, len, 0, l0, 1);
            else l0 += llenww;
        }

        // handle backwards wordwrapped search
        while ((add < 0) && (l0 > text)) {
            char* l1 = line_seek(text, len, 0, l0, -1);
            char* l0_minus1 = l1;
            int nlww = 0; // no of wordwrapped lines in paragraph
            for (char* ld = l1; ld < l0; ld = line_seek(text, len, ww, ld, 1), nlww++)
                l0_minus1 = ld;
            if (add + nlww < 0) { // haven't reached the desired line yet
                add += nlww;
                l0 = l1;
            } else { // reached the desired line
                l0 = (add == -1) ? l0_minus1 : line_seek(text, len, ww, l1, nlww + add);
                add = 0;
            }
        }


        return l0;
    }
}

// checks for illegal ASCII symbols
bool ValidateText(const char* text, u32 len) {
    if (!len) return false;
    for (u32 i = 0; i < len; i++) {
        char c = text[i];
        if ((c == '\r') && ((i+1) < len) && (text[i+1] != '\n')) return false; // CR without LF
        if ((c < 0x20) && (c != '\t') && (c != '\r') && (c != '\n')) return false; // illegal control char
        if (c == 0xFF) return false; // 0xFF illegal char
    }
    return true;
}

void MemTextView(const char* text, u32 len, char* line0, int off_disp, int lno, u32 ww, u32 mno, bool is_script) {
    // block placements
    const char* al_str = "<< ";
    const char* ar_str = " >>";
    u32 x_txt = (TV_LNOS >= 0) ? TV_HPAD + ((TV_LNOS+1)*FONT_WIDTH_EXT) : TV_HPAD;
    u32 x_lno = TV_HPAD;
    u32 p_al = 0;
    u32 p_ar = TV_LLEN_DISP - strlen(ar_str);
    u32 x_al = x_txt + (p_al * FONT_WIDTH_EXT);
    u32 x_ar = x_txt + (p_ar * FONT_WIDTH_EXT);

    // display text on screen
    char txtstr[TV_LLEN_DISP + 1];
    char* ptr = line0;
    u32 nln = lno;
    for (u32 y = TV_VPAD; y < SCREEN_HEIGHT; y += FONT_HEIGHT_EXT + (2*TV_VPAD)) {
        char* ptr_next = line_seek(text, len, ww, ptr, 1);
        u32 llen = line_len(text, len, ww, ptr, NULL);
        u32 ncpy = ((int) llen < off_disp) ? 0 : (llen - off_disp);
        if (ncpy > TV_LLEN_DISP) ncpy = TV_LLEN_DISP;
        bool al = !ww && off_disp && (ptr != ptr_next);
        bool ar = !ww && (llen > off_disp + TV_LLEN_DISP);

        // set text color / find start of comment of scripts
        u32 color_text = (nln == mno) ? script_color_active : (is_script) ? script_color_code : (u32) COLOR_TVTEXT;
        int cmt_start = TV_LLEN_DISP; // start of comment in current displayed line (may be negative)
        if (is_script && (nln != mno)) {
            char* hash = line_seek(text, len, 0, ptr, 0);
            for (; *hash != '#' && (hash - ptr < (int) llen); hash++);
            cmt_start = (hash - ptr) - off_disp;
        }
        if (cmt_start <= 0) color_text = script_color_comment;

        // build text string
        snprintf(txtstr, sizeof(txtstr), "%-*.*s", (int) TV_LLEN_DISP, (int) TV_LLEN_DISP, "");
        if (ncpy) memcpy(txtstr, ptr + off_disp, ncpy);
        for (char* d = txtstr; *d; d++) if (*d < ' ') *d = ' ';
        if (al) memcpy(txtstr + p_al, al_str, strlen(al_str));
        if (ar) memcpy(txtstr + p_ar, ar_str, strlen(ar_str));

        // draw line number & text
        DrawString(TOP_SCREEN, txtstr, x_txt, y, color_text, COLOR_STD_BG);
        if (TV_LNOS > 0) { // line number
            if (ptr != ptr_next)
                DrawStringF(TOP_SCREEN, x_lno, y, ((ptr == text) || (*(ptr-1) == '\n')) ? COLOR_TVOFFS : COLOR_TVOFFSL, COLOR_STD_BG, "%0*lu", TV_LNOS, nln);
            else DrawStringF(TOP_SCREEN, x_lno, y, COLOR_TVOFFSL, COLOR_STD_BG, "%*.*s", TV_LNOS, TV_LNOS, " ");
        }

        // colorize comment if is_script
        if ((cmt_start > 0) && ((u32) cmt_start < TV_LLEN_DISP)) {
            memset(txtstr, ' ', cmt_start);
            DrawString(TOP_SCREEN, txtstr, x_txt, y, script_color_comment, COLOR_TRANSPARENT);
        }

        // colorize arrows
        if (al) DrawStringF(TOP_SCREEN, x_al, y, COLOR_TVOFFS, COLOR_TRANSPARENT, "%s", al_str);
        if (ar) DrawStringF(TOP_SCREEN, x_ar, y, COLOR_TVOFFS, COLOR_TRANSPARENT, "%s", ar_str);

        // advance pointer / line number
        for (char* c = ptr; c < ptr_next; c++) if (*c == '\n') ++nln;
        ptr = ptr_next;
    }
}

bool MemTextViewer(const char* text, u32 len, u32 start, bool as_script) {
    u32 ww = TV_LLEN_DISP;

    // check if this really is text
    if (!ValidateText(text, len)) {
        ShowPrompt(false, "%s", STR_ERROR_INVALID_TEXT_DATA);
        return false;
    }

    // clear screens
    ClearScreenF(true, true, COLOR_STD_BG);

    // instructions
    ShowString("%s", STR_TEXTVIEWER_CONTROLS_DETAILS);

    // set script colors
    if (as_script) {
        script_color_active = COLOR_TVRUN;
        script_color_comment = COLOR_TVCMT;
        script_color_code = COLOR_TVCMD;
    }

    // find maximum line len
    u32 llen_max = 0;
    for (char* ptr = (char*) text; ptr < (text + len); ptr = line_seek(text, len, 0, ptr, 1)) {
        u32 llen = line_len(text, len, 0, ptr, NULL);
        if (llen > llen_max) llen_max = llen;
    }

    // find last allowed lines (ww and nonww)
    char* llast_nww = line_seek(text, len, 0, text + len, -TV_NLIN_DISP);
    char* llast_ww = line_seek(text, len, TV_LLEN_DISP, text + len, -TV_NLIN_DISP);

    char* line0 = (char*) text;
    int lcurr = 1;
    int off_disp = 0;
    for (; lcurr < (int) start; line0 = line_seek(text, len, 0, line0, 1), lcurr++);
    while (true) {
        // display text on screen
        MemTextView(text, len, line0, off_disp, lcurr, ww, 0, as_script);

        // handle user input
        u32 pad_state = InputWait(0);
        char* line0_next = line0;
        u32 step_ud = (pad_state & BUTTON_R1) ? TV_NLIN_DISP : 1;
        u32 step_lr = (pad_state & BUTTON_R1) ? TV_LLEN_DISP : 1;
        bool switched = (pad_state & BUTTON_R1);
        if (pad_state & BUTTON_DOWN) line0_next = line_seek(text, len, ww, line0, step_ud);
        else if (pad_state & BUTTON_UP) line0_next = line_seek(text, len, ww, line0, -step_ud);
        else if (pad_state & BUTTON_RIGHT) off_disp += step_lr;
        else if (pad_state & BUTTON_LEFT) off_disp -= step_lr;
        else if (switched && (pad_state & BUTTON_X)) {
            u64 lnext64 = ShowNumberPrompt(lcurr, STR_CURRENT_LINE_N_ENTER_NEW_LINE_BELOW, lcurr);
            if (lnext64 && (lnext64 != (u64) -1)) line0_next = line_seek(text, len, 0, line0, (int) lnext64 - lcurr);
            ShowString("%s", STR_TEXTVIEWER_CONTROLS_DETAILS);
        } else if (switched && (pad_state & BUTTON_Y)) {
            ww = ww ? 0 : TV_LLEN_DISP;
            line0_next = line_seek(text, len, ww, line0, 0);
        } else if (pad_state & (BUTTON_B|BUTTON_START)) break;

        // check for problems, apply changes
        if (!ww && (line0_next > llast_nww)) line0_next = llast_nww;
        else if (ww && (line0_next > llast_ww)) line0_next = llast_ww;
        if (line0_next < line0) { // fix line number for decrease
            do if (*(--line0) == '\n') lcurr--;
            while (line0 > line0_next);
        } else { // fix line number for increase / same
            for (; line0_next > line0; line0++)
                if (*line0 == '\n') lcurr++;
        }
        if (off_disp + TV_LLEN_DISP > llen_max) off_disp = llen_max - TV_LLEN_DISP;
        if ((off_disp < 0) || ww) off_disp = 0;
    }

    // clear screens
    ClearScreenF(true, true, COLOR_STD_BG);

    return true;
}

// right now really only intended for use with the GodMode9 readme
// (misses safety checks for wider compatibility)
bool MemToCViewer(const char* text, u32 len, const char* title) {
    const u32 max_captions = 24; // we assume this is enough
    char* captions[max_captions];
    u32 lineno[max_captions];
    u32 ww = TV_LLEN_DISP;

    // check if this really is text
    if (!ValidateText(text, len)) {
        ShowPrompt(false, "%s", STR_ERROR_INVALID_TEXT_DATA);
        return false;
    }

    // clear screens / view start of readme on top
    ClearScreenF(true, true, COLOR_STD_BG);
    MemTextView(text, len, (char*) text, 0, 1, ww, 0, false);

    // parse text for markdown captions
    u32 n_captions = 0;
    char* ptr = (char*) text;
    for (u32 lno = 1;; lno++) {
        char* ptr_next = line_seek(text, len, 0, ptr, 1);
        if (ptr == ptr_next) break;
        if (*ptr == '#') {
            captions[n_captions] = ptr;
            lineno[n_captions] = lno;
            if ((lno > 1) && (++n_captions >= max_captions)) break;
        }
        ptr = ptr_next;
    }

    int cursor = -1;
    while (true) {
        // display ToC
        u32 y0 = TV_VPAD;
        u32 x0 = (SCREEN_WIDTH_BOT - GetDrawStringWidth(title)) / 2;
        DrawStringF(BOT_SCREEN, x0, y0, COLOR_TVTEXT, COLOR_STD_BG, "%s\n%*.*s", title,
            strnlen(title, 40), strnlen(title, 40), "========================================");
        y0 += 2 * (FONT_HEIGHT_EXT + (2*TV_VPAD));
        for (u32 i = 0; (i < n_captions) && (y0 < SCREEN_HEIGHT); i++) {
            u32 text_color = ((int) i == cursor) ? COLOR_TVRUN : COLOR_TVTEXT;
            char* caption = captions[i];
            u32 len = 0;
            u32 lvl = 0;
            for (; *caption == '#'; caption++, lvl++);
            for (; IS_WHITESPACE(*caption); caption++);
            for (; caption[len] != '\n' && caption[len] != '\r'; len++);
            DrawStringF(BOT_SCREEN, x0 + (lvl-1) * (FONT_WIDTH_EXT/2), y0, text_color, COLOR_STD_BG,
                "%*.*s", (int) len, (int) len, caption);
            y0 += FONT_HEIGHT_EXT + (2*TV_VPAD);
        }

        // handle user input
        u32 pad_state = InputWait(0);
        if ((cursor >= 0) && (pad_state & BUTTON_A)) {
            if (!MemTextViewer(text, len, lineno[cursor], false)) return false;
            MemTextView(text, len, captions[cursor], 0, lineno[cursor], ww, 0, false);
        } else if (pad_state & BUTTON_B) {
            break;
        } else if (pad_state & BUTTON_UP) {
            cursor = (cursor <= 0) ? ((int) n_captions - 1) : cursor - 1;
            MemTextView(text, len, captions[cursor], 0, lineno[cursor], ww, 0, false);
        } else if (pad_state & BUTTON_DOWN) {
            if (++cursor >= (int) n_captions) cursor = 0;
            MemTextView(text, len, captions[cursor], 0, lineno[cursor], ww, 0, false);
        }
    }

    // clear screens
    ClearScreenF(true, true, COLOR_STD_BG);

    return true;
}

bool FileTextViewer(const char* path, bool as_script) {
    // load text file (completely into memory)
    // text file needs to fit inside the STD_BUFFER_SIZE
    u32 flen, len;

    char* text = malloc(STD_BUFFER_SIZE);
    if (!text) return false;

    flen = FileGetData(path, text, STD_BUFFER_SIZE - 1, 0);

    text[flen] = '\0';
    len = (ptrdiff_t)memchr(text, '\0', flen + 1) - (ptrdiff_t)text;

    // let MemTextViewer take over
    bool result = MemTextViewer(text, len, 1, as_script);

    free(text);
    return result;
}
