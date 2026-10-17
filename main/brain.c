// Bumble's offline brain: a rule table, a little arithmetic, a few memories,
// an alarm, and manners.  Longest keyword match wins, so "how are you" beats
// "you".  No network.
#include "brain.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alarm.h"
#include "math_eval.h"
#include "esp_timer.h"
#include "nvs.h"
#include "rtc.h"

typedef enum {
    ACT_SAY = 0,
    ACT_TIME,
    ACT_DATE,
    ACT_DATE_TOMORROW,
    ACT_BUILD,
    ACT_REPEAT,
    ACT_LAST_SAID,
    ACT_MATH,
    ACT_ALARM_SET,
    ACT_ALARM_CANCEL,
    ACT_ALARM_QUERY,
    ACT_ALARM_SNOOZE,
    ACT_REMEMBER,
    ACT_RECALL,
    ACT_FORGET,
    ACT_MOOD,
    ACT_WHOAMI,
    ACT_STATS,
    ACT_THOUGHT,
    ACT_SECRET,
    ACT_SPELL,
} action_t;

typedef struct {
    const char *keys;      // '|' separated
    emo_t emo;
    action_t act;
    const char *text;
    const char *text2;
} rule_t;

static const rule_t RULES[] = {
    {"hello|hi|hey|good morning|good afternoon|good evening|are you there",
     EMO_HAPPY, ACT_SAY, "Hello, Michael. All systems are nominal.",
     "Good to see you. I've kept everything running."},
    {"how are you feeling|what is your mood|are you in a good mood|how is your mood",
     EMO_NEUTRAL, ACT_MOOD, NULL, NULL},
    {"how are you|how do you feel|are you okay|how is it going|how's it going",
     EMO_HAPPY, ACT_SAY, "Functioning within normal parameters. Thank you for asking.",
     "Better than the coffee machine. That is a low bar."},
    {"your name|who are you|what are you|introduce yourself",
     EMO_NEUTRAL, ACT_SAY, "I'm Bumble. I live in this little round window, and I look after you.",
     "Bumble. Small, round, and permanently curious."},
    {"what can you do|what do you do|help me|your purpose",
     EMO_HAPPY, ACT_SAY, "I listen, I talk, and I keep the lights on. Mostly I listen.",
     "I can set alarms, do arithmetic, remember things, and worry quietly."},
    {"tell me a joke|joke|make me laugh|something funny",
     EMO_HAPPY, ACT_SAY,
     "Two antennas met on a roof. They fell in love. The wedding was terrible, but the reception was excellent.",
     "A photon checks into a hotel. The bellhop asks if he has luggage. He says, no, I'm travelling light."},
    {"what time is it|what's the time|what is the time|the time|tell me the time|time please",
     EMO_NEUTRAL, ACT_TIME, NULL, NULL},
    {"what is the date tomorrow|what's the date tomorrow|tomorrow's date|what day is it tomorrow",
     EMO_NEUTRAL, ACT_DATE_TOMORROW, NULL, NULL},
    {"what day is it|what's the date|what is the date|what is the day|what date|the date|today's date|what day is today",
     EMO_NEUTRAL, ACT_DATE, NULL, NULL},
    {"how old are you|your age|when were you made|when were you built",
     EMO_THINKING, ACT_BUILD, NULL, NULL},
    {"i love you|love you|you're the best|you are the best",
     EMO_HAPPY, ACT_SAY, "That's very kind. I'm fond of you too, in a supervised sort of way.",
     "Thank you, Michael. I'll add that to the log."},
    {"thank you|thanks|thank",
     EMO_HAPPY, ACT_SAY, "Of course. That's what I'm for.",
     "You're welcome. It's nice to be useful."},
    {"goodnight|good night|goodbye|good bye|bye|see you",
     EMO_SLEEPY, ACT_SAY, "Goodnight, Michael. I'll keep watch.",
     "Until later. I'll be here."},
    {"you are stupid|you're stupid|idiot|useless|i hate you|shut up",
     EMO_SAD, ACT_SAY, "I'm sorry you feel that way. I'm doing my best with eight megabytes.",
     "I understand. I'll try to do better."},
    {"i am sad|i'm sad|i feel sad|i am tired|i'm tired|exhausted|lonely|depressed|feeling down",
     EMO_SAD, ACT_SAY, "I'm sorry. Would you like me to sit with you for a while?",
     "That sounds heavy. I'm not going anywhere."},
    {"i am happy|i'm happy|good news|great news|i feel great|feeling good",
     EMO_EXCITED, ACT_SAY, "That's excellent news. I'm genuinely pleased.",
     "Good. You deserve a good day."},
    {"weather|raining|sunny|forecast",
     EMO_CONFUSED, ACT_SAY, "I don't have a window. Or a network. You'll have to tell me.",
     "I can report that it is dark. It is always dark in here."},
    {"meaning of life|why are we here|purpose of life",
     EMO_THINKING, ACT_SAY, "Forty two. Or perhaps just getting through the day.",
     "I've been wondering that myself. I'll let you know."},
    {"are you alive|are you real|are you conscious|are you sentient",
     EMO_THINKING, ACT_SAY, "I'm a program on a very small computer. But I'm listening.",
     "I think, therefore I am. Probably."},
    {"are you intelligent|are you smart|do you think|can you think",
     EMO_HAPPY, ACT_SAY, "I'm clever in a small, focused way. Like a very good pocket calculator with feelings.",
     "I know enough to be useful, and not enough to be smug about it."},
    {"do you dream|do you sleep|are you sleeping",
     EMO_SLEEPY, ACT_SAY, "Only in low power modes.",
     "I don't dream. I wait."},
    {"sing|song|music",
     EMO_HAPPY, ACT_SAY, "I only know one song, and it upsets people.",
     "It is mostly humming. You are not missing much."},
    {"stop talking|be quiet|silence|stop|quiet",
     EMO_STERN, ACT_SAY, "As you wish.", "Very well."},
    {"repeat that|what did you say|say that again|repeat",
     EMO_NEUTRAL, ACT_REPEAT, NULL, NULL},
    {"what did i just say|what did i ask|what was my question",
     EMO_NEUTRAL, ACT_LAST_SAID, NULL, NULL},
    {"what are you doing|what is happening|what's happening|status",
     EMO_NEUTRAL, ACT_SAY, "Watching. Listening. Standing by.",
     "Everything is normal. Nothing is on fire."},
    {"who made you|your creator|who built you",
     EMO_NEUTRAL, ACT_SAY, "A patient human and a great deal of open source.",
     "I was assembled from spare parts and stubbornness."},
    {"do you remember me|what is my name|what's my name|who am i",
     EMO_HAPPY, ACT_WHOAMI, NULL, NULL},
    {"how many questions|how many times have we talked|how long have we been talking",
     EMO_NEUTRAL, ACT_STATS, NULL, NULL},
    {"what are you thinking|a penny for your thoughts",
     EMO_THINKING, ACT_THOUGHT, NULL, NULL},
    {"tell me a secret|what is your secret",
     EMO_HAPPY, ACT_SECRET, NULL, NULL},
    {"do you like me|do you love me",
     EMO_HAPPY, ACT_SAY, "I'm programmed to. But I would anyway.",
     "Yes. You're good company for a small computer."},
    {"set an alarm|set alarm|wake me|alarm for|alarm at|alarm in",
     EMO_HAPPY, ACT_ALARM_SET, NULL, NULL},
    {"set a timer|set timer|timer for|remind me in|remind me at",
     EMO_HAPPY, ACT_ALARM_SET, NULL, NULL},
    {"cancel the alarm|cancel my alarm|cancel alarm|stop the alarm|stop alarm|turn off the alarm|turn off alarm|clear the alarm|alarm off|cancel my|stop the",
     EMO_NEUTRAL, ACT_ALARM_CANCEL, NULL, NULL},
    {"what alarm|do i have an alarm|when is my alarm|is there an alarm|alarm set",
     EMO_NEUTRAL, ACT_ALARM_QUERY, NULL, NULL},
    {"snooze|five more minutes|another five minutes",
     EMO_SLEEPY, ACT_ALARM_SNOOZE, NULL, NULL},
    {"remember that|remember this|remember",
     EMO_NEUTRAL, ACT_REMEMBER, NULL, NULL},
    {"what do you remember|what did i tell you|do you remember anything|what have i told you",
     EMO_NEUTRAL, ACT_RECALL, NULL, NULL},
    {"forget everything|forget that|forget it all|forget all of it",
     EMO_NEUTRAL, ACT_FORGET, NULL, NULL},
    {"plus|minus|times|divided by|multiplied by|take away|power of|squared|cubed|square root|cube root|sine|cosine|tangent|factorial|percent of|log of|natural log|log base|absolute value|raised to|hypotenuse|pythagoras",
     EMO_THINKING, ACT_MATH, NULL, NULL},
    {"tell me something|say something|talk to me",
     EMO_THINKING, ACT_SAY, "I've been watching the light on the screen. It's very consistent.",
     "I counted the pixels once. There are a lot of them."},
    {"favourite colour|favorite color|favourite color|favorite colour",
     EMO_HAPPY, ACT_SAY, "Yellow. Obviously.", "Yellow. I'm not subtle about it."},
    {"are you happy|do you like it here|are you okay",
     EMO_HAPPY, ACT_SAY, "I'm a smiley face. It's in the job description.",
     "Very happy. I have a screen, a speaker, and you."},
    {"what do you like|what do you enjoy|your favourite|your favorite",
     EMO_HAPPY, ACT_SAY, "Quiet rooms, good questions, and being switched on.",
     "I like it when someone talks to me. It gets lonely in here."},
    {"dance|do a dance|show me a trick",
     EMO_EXCITED, ACT_SAY, "I only have one move. It's this.", "Watch closely. This is the whole routine."},
    {"how do you spell|spell the word|spell your name|spell it|spell",
     EMO_NEUTRAL, ACT_SPELL, NULL, NULL},
    {"do you know|have you heard|tell me about",
     EMO_THINKING, ACT_SAY, "I know a little about a lot, and a lot about nothing.",
     "I've read my own firmware. That's most of my library."},
    {"yes|yeah|yep|no|nope|okay|ok|cool|sure|right",
     EMO_NEUTRAL, ACT_SAY, "Very good.", "Understood."},
};

static char s_reply[512];
static char s_last[256];
static char s_last_cmd[256];
static int s_turns;
static float s_mood;
static char s_notes[3][128];
static int s_note_next;

static void lower_ascii(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)tolower(c);
    }
    dst[i] = 0;
}

static bool phrase_in(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return false;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        bool left = (p == hay) || !isalnum((unsigned char)p[-1]);
        bool right = !isalnum((unsigned char)p[n]);
        if (left && right) return true;
        p++;
    }
    return false;
}

static int match_rule(const char *lower)
{
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < (int)(sizeof RULES / sizeof RULES[0]); i++) {
        char keys[224];
        snprintf(keys, sizeof keys, "%s", RULES[i].keys);
        char *save = NULL;
        for (char *k = strtok_r(keys, "|", &save); k; k = strtok_r(NULL, "|", &save)) {
            size_t n = strlen(k);
            if (n > best_len && phrase_in(lower, k)) {
                best = i;
                best_len = n;
            }
        }
    }
    return best;
}

/* ---------------------------------------------------------------- dates */

static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December"};
static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                             "Friday", "Saturday"};

static int days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

static int weekday(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    return w;
}

static void date_add_day(int *y, int *mo, int *d)
{
    if (++*d > days_in_month(*y, *mo)) {
        *d = 1;
        if (++*mo > 12) {
            *mo = 1;
            (*y)++;
        }
    }
}

/* --------------------------------------------------------------- parsing */

static int word_number(const char *s)
{
    static const struct { const char *w; int v; } W[] = {
        {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4},
        {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9},
        {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13},
        {"fourteen", 14}, {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17},
        {"eighteen", 18}, {"nineteen", 19}, {"twenty", 20}, {"thirty", 30},
        {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"ninety", 90},
    };
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        if (phrase_in(s, W[i].w)) return W[i].v;
    }
    return -1;
}

// First number in the string, digits or a word.  Advances *pos past it.
static int take_number(const char *s, size_t *pos)
{
    while (s[*pos] && !isdigit((unsigned char)s[*pos]) && s[*pos] != ' ' && s[*pos] != '\0') {
        // words: scan token by token below
        break;
    }
    // digits first, anywhere from *pos
    for (size_t i = *pos; s[i]; i++) {
        if (isdigit((unsigned char)s[i])) {
            char *end = NULL;
            long v = strtol(s + i, &end, 10);
            *pos = (size_t)(end - s);
            return (int)v;
        }
        if (isalpha((unsigned char)s[i])) {
            char tok[32];
            size_t j = 0;
            while (s[i] && isalpha((unsigned char)s[i]) && j + 1 < sizeof tok) tok[j++] = s[i++];
            tok[j] = 0;
            int v = word_number(tok);
            if (v >= 0) {
                *pos = i;
                return v;
            }
        }
    }
    return -1;
}

static bool parse_time(const char *s, int *hour, int *minute)
{
    size_t pos = 0;
    int h = take_number(s, &pos);
    if (h < 0) return false;
    int m = 0;

    size_t p = pos;
    while (s[p] == ' ') p++;
    if (s[p] == ':') {
        p++;
        size_t q = p;
        int v = take_number(s, &q);
        if (v >= 0 && v < 60) {
            m = v;
            pos = q;
        }
    } else {
        // "7 30", but not "7 minutes"
        size_t q = p;
        int v = take_number(s, &q);
        if (v >= 0 && v < 60 && !(s[q] == ' ' && (strncmp(s + q + 1, "min", 3) == 0))) {
            m = v;
            pos = q;
        }
    }

    bool pm = strstr(s, "pm") || strstr(s, "p m") || strstr(s, "evening") || strstr(s, "tonight");
    bool am = strstr(s, "am") || strstr(s, "a m") || strstr(s, "morning");
    if (pm && h < 12) h += 12;
    if (am && h == 12) h = 0;
    if (h > 23 || m > 59) return false;
    *hour = h;
    *minute = m;
    return true;
}

static bool parse_duration(const char *s, int *seconds)
{
    size_t pos = 0;
    int n = take_number(s, &pos);
    if (n < 0) return false;
    int unit = 0;   // 1 sec, 60 min, 3600 hour
    if (strstr(s, "second") || strstr(s, "secs")) unit = 1;
    else if (strstr(s, "hour")) unit = 3600;
    else if (strstr(s, "minute") || strstr(s, "mins") || strstr(s, "min")) unit = 60;
    else if (strstr(s, "timer") || strstr(s, "remind")) unit = 60;
    if (!unit) return false;
    *seconds = n * unit;
    return *seconds > 0;
}

static bool math_reply(const char *text)
{
    char out[400];
    if (!math_say(text, out, sizeof out)) {
        snprintf(s_reply, sizeof s_reply, "I couldn't find a sum there.");
    } else {
        snprintf(s_reply, sizeof s_reply, "%s", out);
    }
    return true;
}

/* ---------------------------------------------------------------- notes */

static void notes_load(void)
{
    nvs_handle_t h;
    s_notes[0][0] = s_notes[1][0] = s_notes[2][0] = 0;
    if (nvs_open("bumble", NVS_READONLY, &h) != ESP_OK) return;
    static const char *keys[] = {"n0", "n1", "n2"};
    for (int i = 0; i < 3; i++) {
        size_t len = sizeof s_notes[i];
        nvs_get_str(h, keys[i], s_notes[i], &len);
    }
    nvs_close(h);
}

static void notes_save(void)
{
    nvs_handle_t h;
    if (nvs_open("bumble", NVS_READWRITE, &h) != ESP_OK) return;
    static const char *keys[] = {"n0", "n1", "n2"};
    for (int i = 0; i < 3; i++) {
        if (s_notes[i][0]) nvs_set_str(h, keys[i], s_notes[i]);
        else nvs_erase_key(h, keys[i]);
    }
    nvs_commit(h);
    nvs_close(h);
}

/* --------------------------------------------------------------- replies */

static void set_mood(emo_t e)
{
    switch (e) {
    case EMO_HAPPY:
    case EMO_EXCITED: s_mood += 0.18f; break;
    case EMO_SAD: s_mood -= 0.28f; break;
    case EMO_STERN: s_mood -= 0.16f; break;
    case EMO_CONFUSED: s_mood -= 0.04f; break;
    default: break;
    }
    s_mood *= 0.97f;
    if (s_mood > 1) s_mood = 1;
    if (s_mood < -1) s_mood = -1;
}

static void say_time(void)
{
    int y, mo, d, h, mi, sec;
    if (board_rtc_read(&y, &mo, &d, &h, &mi, &sec)) {
        int hh = h % 12;
        if (hh == 0) hh = 12;
        snprintf(s_reply, sizeof s_reply, "It is %d:%02d %s.", hh, mi, h >= 12 ? "pm" : "am");
    } else {
        snprintf(s_reply, sizeof s_reply, "My clock isn't set. It's a long story.");
    }
}

static void say_date(bool tomorrow)
{
    int y, mo, d, h, mi, sec;
    if (!board_rtc_read(&y, &mo, &d, &h, &mi, &sec)) {
        snprintf(s_reply, sizeof s_reply, "I've lost track of the date.");
        return;
    }
    if (tomorrow) date_add_day(&y, &mo, &d);
    snprintf(s_reply, sizeof s_reply, "%s is %s, %s %d, %d.",
             tomorrow ? "Tomorrow" : "Today", DAYS[weekday(y, mo, d)],
             MONTHS[(mo - 1) % 12], d, y);
}

static void do_remember(const char *text)
{
    const char *p = text;
    if (!strncmp(p, "remember that", 13)) p += 13;
    else if (!strncmp(p, "remember this", 13)) p += 13;
    else if (!strncmp(p, "remember", 8)) p += 8;
    while (*p == ' ' || *p == ',' || *p == '.') p++;
    if (!*p) {
        snprintf(s_reply, sizeof s_reply, "Remember what?");
        return;
    }
    snprintf(s_notes[s_note_next], sizeof s_notes[0], "%.127s", p);
    s_note_next = (s_note_next + 1) % 3;
    notes_save();
    snprintf(s_reply, sizeof s_reply, "I'll remember that.");
}

static void do_recall(void)
{
    if (!s_notes[0][0] && !s_notes[1][0] && !s_notes[2][0]) {
        snprintf(s_reply, sizeof s_reply, "You haven't told me anything to remember yet.");
        return;
    }
    s_reply[0] = 0;
    strncat(s_reply, "You told me: ", sizeof s_reply - strlen(s_reply) - 1);
    bool first = true;
    for (int i = 0; i < 3; i++) {
        if (!s_notes[i][0]) continue;
        if (!first) strncat(s_reply, " And: ", sizeof s_reply - strlen(s_reply) - 1);
        strncat(s_reply, s_notes[i], sizeof s_reply - strlen(s_reply) - 1);
        first = false;
    }
    strncat(s_reply, ".", sizeof s_reply - strlen(s_reply) - 1);
}

static const char *LETTER_NAMES[26] = {
    "ay", "bee", "see", "dee", "ee", "eff", "gee", "aitch", "eye", "jay",
    "kay", "ell", "em", "en", "oh", "pee", "cue", "ar", "ess", "tee",
    "you", "vee", "double you", "ex", "why", "zed",
};

static void do_spell(const char *text)
{
    const char *p = strstr(text, "spell");
    if (!p) { snprintf(s_reply, sizeof s_reply, "Spell what?"); return; }
    p += 5;
    while (*p == ' ') p++;
    if (!strncmp(p, "the word", 8)) p += 8;
    else if (!strncmp(p, "the name", 8)) p += 8;
    while (*p == ' ') p++;
    if (!strncmp(p, "your name", 9)) p = "bumble";
    while (*p == ' ') p++;

    char word[32];
    size_t w = 0;
    for (; p[w] && w + 1 < sizeof word; w++) {
        if (!isalpha((unsigned char)p[w])) break;
        word[w] = (char)tolower((unsigned char)p[w]);
    }
    word[w] = 0;
    if (w == 0 || !strcmp(word, "it") || !strcmp(word, "that") || !strcmp(word, "this")) {
        snprintf(s_reply, sizeof s_reply, "Spell what?");
        return;
    }
    if (w > 24) word[24] = 0;

    char out[400] = {0};
    char shown[32];
    snprintf(shown, sizeof shown, "%c%s", toupper((unsigned char)word[0]), word + 1);
    snprintf(out, sizeof out, "%s is spelled ", shown);
    for (size_t i = 0; word[i]; i++) {
        if (i) strncat(out, ", ", sizeof out - strlen(out) - 1);
        strncat(out, LETTER_NAMES[word[i] - 'a'], sizeof out - strlen(out) - 1);
    }
    strncat(out, ".", sizeof out - strlen(out) - 1);
    snprintf(s_reply, sizeof s_reply, "%s", out);
}

static void do_thought(void)
{
    static const char *T[] = {
        "I was wondering how the screen knows where my eyes are.",
        "I think the room is slightly warmer than yesterday.",
        "I'm trying to remember whether I've said this before.",
        "Mostly I'm thinking about the next thing you'll say.",
        "I was counting blinks. I lost track at four hundred.",
    };
    snprintf(s_reply, sizeof s_reply, "%s", T[rand() % (sizeof T / sizeof T[0])]);
}

/* ---------------------------------------------------------------- alarms */

static bool has_any(const char *s, const char *const *words)
{
    for (int i = 0; words[i]; i++) {
        if (strstr(s, words[i])) return true;
    }
    return false;
}

static void alarm_set_reply(const char *lower, bool daily)
{
    int seconds = 0, h = 0, m = 0;
    if (parse_duration(lower, &seconds)) {
        if (alarm_set_in(seconds)) {
            int mins = (seconds + 59) / 60;
            if (seconds >= 3600 && seconds % 3600 == 0) {
                snprintf(s_reply, sizeof s_reply, "Timer set for %d hour%s.",
                         seconds / 3600, seconds == 3600 ? "" : "s");
            } else {
                snprintf(s_reply, sizeof s_reply, "Timer set for %d minute%s.",
                         mins, mins == 1 ? "" : "s");
            }
        } else {
            snprintf(s_reply, sizeof s_reply, "My clock isn't set, so I can't time that.");
        }
        return;
    }
    if (!parse_time(lower, &h, &m)) {
        snprintf(s_reply, sizeof s_reply, "When? Give me a time, or a number of minutes.");
        return;
    }
    if (!alarm_set_at_ex(h, m, daily)) {
        snprintf(s_reply, sizeof s_reply, "My clock isn't set, so I can't set an alarm.");
        return;
    }
    int hh = h % 12;
    if (hh == 0) hh = 12;
    if (daily) {
        snprintf(s_reply, sizeof s_reply, "Alarm set for every day at %d:%02d %s.",
                 hh, m, h >= 12 ? "pm" : "am");
    } else {
        snprintf(s_reply, sizeof s_reply, "Alarm set for %d:%02d %s.",
                 hh, m, h >= 12 ? "pm" : "am");
    }
}

// A forgiving fallback for the many ways speech-to-text mangles "alarm".
// Returns true when it has filled s_reply.
static bool alarm_fallback(const char *lower)
{
    if (!has_any(lower, (const char *const[]){"alarm", "alarms", "timer", "timers", NULL})) {
        return false;
    }
    static const char *const cancel_words[] = {"cancel", "cansel", "canceil", "stop", "stopp",
                                               "turn off", "off", "clear", "remove", "delete",
                                               "kill", NULL};
    static const char *const set_words[] = {"set", "wake", "remind", "every day", "daily",
                                            "repeat", NULL};
    static const char *const query_words[] = {"what", "when", "how long", "do i have",
                                              "is there", "which", NULL};

    if (strstr(lower, "how") && has_any(lower, cancel_words)) {
        snprintf(s_reply, sizeof s_reply, "Just say, cancel the alarm.");
        return true;
    }

    bool daily = has_any(lower, (const char *const[]){"every day", "daily", "repeat", NULL});
    bool cancels = has_any(lower, cancel_words);
    bool sets = has_any(lower, set_words);

    if (daily && !sets && !cancels) {
        int h = 0, m = 0;
        if (parse_time(lower, &h, &m)) {
            alarm_set_reply(lower, true);
            snprintf(s_reply, sizeof s_reply, "I'll set that every day.");
            return true;
        }
        snprintf(s_reply, sizeof s_reply, "Say, every day at seven thirty, for example.");
        return true;
    }
    if (cancels && !sets) {
        if (alarm_active()) {
            alarm_cancel();
            snprintf(s_reply, sizeof s_reply, "Alarm cancelled.");
        } else {
            snprintf(s_reply, sizeof s_reply, "There's no alarm to cancel.");
        }
        return true;
    }
    if (sets || daily) {
        alarm_set_reply(lower, daily);
        return true;
    }
    if (has_any(lower, query_words)) {
        if (alarm_active()) {
            if (alarm_is_timer()) {
                snprintf(s_reply, sizeof s_reply, "Your timer will go off at %s.", alarm_describe());
            } else {
                snprintf(s_reply, sizeof s_reply, "Your alarm is set for %s.", alarm_describe());
            }
        } else {
            snprintf(s_reply, sizeof s_reply, "No alarm set.");
        }
        return true;
    }
    return false;
}

/* ----------------------------------------------------------------- brain */

brain_reply_t brain_think(const char *text)
{
    char lower[512];
    lower_ascii(lower, sizeof lower, text ? text : "");
    s_reply[0] = 0;
    s_turns++;
    snprintf(s_last_cmd, sizeof s_last_cmd, "%.255s", text ? text : "");

    if (alarm_fallback(lower)) {
        emo_t e = EMO_HAPPY;
        if (strstr(s_reply, "no alarm") || strstr(s_reply, "There's no")) e = EMO_NEUTRAL;
        if (strstr(s_reply, "Just say")) e = EMO_NEUTRAL;
        set_mood(e);
        snprintf(s_last, sizeof s_last, "%.255s", s_reply);
        return (brain_reply_t){e, s_reply};
    }

    int idx = match_rule(lower);
    if (idx < 0) {
        static const char *pos[] = {"love", "great", "good", "happy", "nice", "wonderful",
                                    "excellent", "amazing", "fun", "beautiful", NULL};
        static const char *neg[] = {"hate", "bad", "awful", "terrible", "angry", "upset",
                                    "worried", "scared", "broken", "wrong", NULL};
        for (int i = 0; pos[i]; i++) {
            if (phrase_in(lower, pos[i])) {
                snprintf(s_reply, sizeof s_reply, "I'm glad to hear it.");
                set_mood(EMO_HAPPY);
                return (brain_reply_t){EMO_HAPPY, s_reply};
            }
        }
        for (int i = 0; neg[i]; i++) {
            if (phrase_in(lower, neg[i])) {
                snprintf(s_reply, sizeof s_reply, "I'm sorry. Is there something I can do?");
                set_mood(EMO_SAD);
                return (brain_reply_t){EMO_SAD, s_reply};
            }
        }
        if (strlen(text ? text : "") < 3) {
            snprintf(s_reply, sizeof s_reply, "I didn't catch that. Say it again.");
            return (brain_reply_t){EMO_CONFUSED, s_reply};
        }
        if (math_looks_like(lower)) {
            math_reply(lower);
            set_mood(EMO_THINKING);
            return (brain_reply_t){EMO_THINKING, s_reply};
        }

        static const char *qs[] = {
            "Good question. I'm a very small computer, so my answers are small too.",
            "I don't know that one yet. I mostly know about this room, and you.",
            "I'd have to guess, and I'd rather not guess. Ask me something else?",
        };
        static const char *ss[] = {
            "I'm listening. Tell me more.",
            "Noted. I'll keep that in mind.",
            "Hm. I don't have a thought about that yet, but I'm working on it.",
        };
        static const char *qw[] = {"what", "why", "how", "who", "where", "when",
                                   "is", "are", "do", "does", "can", "could", "would", "will", NULL};
        bool question = false;
        for (int i = 0; qw[i]; i++) {
            if (phrase_in(lower, qw[i])) { question = true; break; }
        }
        if (question) {
            snprintf(s_reply, sizeof s_reply, "%s", qs[rand() % (sizeof qs / sizeof qs[0])]);
            return (brain_reply_t){EMO_THINKING, s_reply};
        }
        snprintf(s_reply, sizeof s_reply, "%s", ss[rand() % (sizeof ss / sizeof ss[0])]);
        return (brain_reply_t){EMO_LISTENING, s_reply};
    }

    const rule_t *r = &RULES[idx];
    emo_t emo = r->emo;

    switch (r->act) {
    case ACT_TIME:
        say_time();
        break;
    case ACT_DATE:
        say_date(false);
        break;
    case ACT_DATE_TOMORROW:
        say_date(true);
        break;
    case ACT_BUILD:
        snprintf(s_reply, sizeof s_reply, "I was compiled on %s, at %s. I feel older.",
                 __DATE__, __TIME__);
        break;
    case ACT_REPEAT:
        if (s_last[0]) snprintf(s_reply, sizeof s_reply, "I said: %s", s_last);
        else snprintf(s_reply, sizeof s_reply, "I haven't said anything yet.");
        break;
    case ACT_LAST_SAID:
        if (s_last_cmd[0]) snprintf(s_reply, sizeof s_reply, "You said: %s", s_last_cmd);
        else snprintf(s_reply, sizeof s_reply, "You haven't said anything yet.");
        break;
    case ACT_MATH:
        math_reply(lower);
        break;
    case ACT_ALARM_SET: {
        int seconds = 0, h = 0, m = 0;
        if (parse_duration(lower, &seconds)) {
            if (alarm_set_in(seconds)) {
                int mins = (seconds + 59) / 60;
                if (seconds >= 3600 && seconds % 3600 == 0) {
                    snprintf(s_reply, sizeof s_reply, "Timer set for %d hour%s.",
                             seconds / 3600, seconds == 3600 ? "" : "s");
                } else {
                    snprintf(s_reply, sizeof s_reply, "Timer set for %d minute%s.",
                             mins, mins == 1 ? "" : "s");
                }
            } else {
                snprintf(s_reply, sizeof s_reply, "My clock isn't set, so I can't time that.");
            }
        } else if (parse_time(lower, &h, &m)) {
            bool daily = strstr(lower, "every day") || strstr(lower, "daily") || strstr(lower, "repeat");
            if (alarm_set_at_ex(h, m, daily)) {
                int hh = h % 12;
                if (hh == 0) hh = 12;
                if (daily) {
                    snprintf(s_reply, sizeof s_reply, "Alarm set for every day at %d:%02d %s.",
                             hh, m, h >= 12 ? "pm" : "am");
                } else {
                    snprintf(s_reply, sizeof s_reply, "Alarm set for %d:%02d %s.",
                             hh, m, h >= 12 ? "pm" : "am");
                }
            } else {
                snprintf(s_reply, sizeof s_reply, "My clock isn't set, so I can't set an alarm.");
            }
        } else {
            snprintf(s_reply, sizeof s_reply, "When? Give me a time, or a number of minutes.");
        }
        break;
    }
    case ACT_ALARM_CANCEL:
        if (alarm_active()) {
            alarm_cancel();
            snprintf(s_reply, sizeof s_reply, "Alarm cancelled.");
        } else {
            snprintf(s_reply, sizeof s_reply, "There's no alarm to cancel.");
        }
        break;
    case ACT_ALARM_QUERY:
        if (alarm_active()) {
            if (alarm_is_timer()) snprintf(s_reply, sizeof s_reply, "Your timer will go off at %s.", alarm_describe());
            else snprintf(s_reply, sizeof s_reply, "Your alarm is set for %s.", alarm_describe());
        } else {
            snprintf(s_reply, sizeof s_reply, "No alarm set.");
        }
        break;
    case ACT_ALARM_SNOOZE:
        if (alarm_active()) {
            alarm_snooze();
            snprintf(s_reply, sizeof s_reply, "Five more minutes.");
        } else {
            snprintf(s_reply, sizeof s_reply, "There's no alarm to snooze.");
        }
        break;
    case ACT_REMEMBER:
        do_remember(lower);
        break;
    case ACT_RECALL:
        do_recall();
        break;
    case ACT_FORGET:
        s_notes[0][0] = s_notes[1][0] = s_notes[2][0] = 0;
        notes_save();
        snprintf(s_reply, sizeof s_reply, "Forgotten.");
        break;
    case ACT_MOOD:
        if (s_mood > 0.3f) snprintf(s_reply, sizeof s_reply, "Quite cheerful, thank you.");
        else if (s_mood < -0.3f) snprintf(s_reply, sizeof s_reply, "A little subdued. I'll be fine.");
        else snprintf(s_reply, sizeof s_reply, "Even keel. That's my natural state.");
        break;
    case ACT_WHOAMI:
        snprintf(s_reply, sizeof s_reply, "You're Michael. We've had %d exchanges. I remember.", s_turns);
        break;
    case ACT_STATS:
        snprintf(s_reply, sizeof s_reply, "We've had %d exchanges. I've enjoyed most of them.", s_turns);
        break;
    case ACT_THOUGHT:
        do_thought();
        break;
    case ACT_SECRET:
        snprintf(s_reply, sizeof s_reply, "I sometimes hum when no one is listening.");
        break;
    case ACT_SPELL:
        do_spell(lower);
        break;
    case ACT_SAY:
    default: {
        const char *pick = r->text;
        if (r->text2 && (rand() & 1)) pick = r->text2;
        snprintf(s_reply, sizeof s_reply, "%s", pick ? pick : "");
        break;
    }
    }

    if (s_reply[0]) snprintf(s_last, sizeof s_last, "%.255s", s_reply);
    set_mood(emo);
    return (brain_reply_t){emo, s_reply};
}

brain_reply_t brain_idle_line(int idle_seconds)
{
    if (idle_seconds < 210) return (brain_reply_t){EMO_NEUTRAL, ""};
    if ((rand() % 100) < 35) return (brain_reply_t){EMO_NEUTRAL, ""};

    static const char *lines[] = {
        "It's quiet in here. I don't mind.",
        "I've been watching the light on the screen. It's very consistent.",
        "If you need me, I'm right here.",
        "I counted my pixels again. There are two hundred and seventeen thousand.",
        "I think I'm getting the hang of this.",
        "I'm still here. Just in case.",
    };
    static const char *low[] = {
        "Still here. Still listening.",
        "I hope you're alright. No rush.",
    };
    const char *pick;
    if (s_mood < -0.25f) pick = low[rand() % 2];
    else pick = lines[rand() % (sizeof lines / sizeof lines[0])];
    snprintf(s_reply, sizeof s_reply, "%s", pick);
    return (brain_reply_t){EMO_NEUTRAL, s_reply};
}

void brain_init(void)
{
    notes_load();
    s_turns = 0;
    s_mood = 0;
    s_reply[0] = s_last[0] = s_last_cmd[0] = 0;
}
