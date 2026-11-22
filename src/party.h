#ifndef DND_PARTY_H
#define DND_PARTY_H

enum { PARTY_SIZE = 3 };

typedef struct {
    const char *name;
    int hp;
    int attack;
    int dexterity_modifier;
} Character;

typedef struct {
    Character members[PARTY_SIZE];
} Party;

#endif
