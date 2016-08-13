#include "chess.h"

// TODO: get rid of this and perform everything in QBBs!

void HexaToQuadBB(QuadBitBoardPosition *qbb, GameState *gameState, HexaBitBoardPosition *hbb)
{
    gameState->chance           = hbb->chance;
    gameState->whiteCastle      = hbb->whiteCastle;
    gameState->blackCastle      = hbb->blackCastle;
    gameState->enPassent        = hbb->enPassent;
    gameState->halfMoveCounter  = hbb->halfMoveCounter;

    qbb->white = hbb->whitePieces;
    
    uint64 knights = hbb->knights;
    uint64 queens  = hbb->bishopQueens & hbb->rookQueens;
    uint64 bishops = hbb->bishopQueens & ~queens;
    uint64 rooks   = hbb->rookQueens   & ~queens;
    uint64 pawns   = hbb->pawns & RANKS2TO7;
    uint64 kings   = hbb->kings;

    qbb->nbk = knights | bishops | kings;
    qbb->pbq = pawns   | bishops | queens;
    qbb->rqk = rooks   | queens  | kings;
}

void quadToHexaBB(HexaBitBoardPosition *hbb, QuadBitBoardPosition *qbb, GameState *gameState)
{
    uint64 bishops = qbb->nbk & qbb->pbq;
    uint64 queens  = qbb->pbq & qbb->rqk;
    uint64 kings   = qbb->nbk & qbb->rqk;

    uint64 odd     = qbb->nbk ^ qbb->pbq ^ qbb->rqk;  // pawn or knight or rook
    uint64 pawns   = odd & qbb->pbq;
    uint64 knights = odd & qbb->nbk;
    uint64 rooks   = odd & qbb->rqk;

    hbb->bishopQueens = bishops | queens;
    hbb->rookQueens   = rooks | queens;
    hbb->kings   = kings;
    hbb->knights = knights;
    hbb->pawns   = pawns;
    hbb->whitePieces = qbb->white;

    hbb->chance = gameState->chance;
    hbb->whiteCastle = gameState->whiteCastle;
    hbb->blackCastle = gameState->blackCastle;
    hbb->enPassent = gameState->enPassent;
    hbb->halfMoveCounter = gameState->halfMoveCounter;
}

#pragma pack(push, 1)
struct UniquePosRecord
{
    uint64                  hash;    //  8 bytes hash should be enough as we aren't dealing with too many positions yet
    QuadBitBoardPosition    pos;     // 32 bytes
    GameState               state;   //  2 bytes
    uint32                  count;   //  4 bytes
    uint32                  next;    //  4 bytes index to next record (for chaining), ~0 for invalid/null
};

struct FileRecord
{
    QuadBitBoardPosition    pos;     // 32 bytes
    uint32 count        : 23;
    uint32 chance       : 1;
    uint32 whiteCastle  : 2;
    uint32 blackCastle  : 2;
    uint32 enPassent    : 4;
};
#pragma pack(pop)
CT_ASSERT(sizeof(FileRecord)      == 36);
CT_ASSERT(sizeof(UniquePosRecord) == 50);

// 26 bits -> 64 million entries: 3200 MB hash table
#define UNIQUE_TABLE_BITS           26
#define UNIQUE_TABLE_SIZE           (1 << UNIQUE_TABLE_BITS)
#define UNIQUE_TABLE_INDEX_BITS     (UNIQUE_TABLE_SIZE - 1)
#define UNIQUE_TABLE_HASH_BITS      (ALLSET ^ UNIQUE_TABLE_INDEX_BITS)

// hash table to store unique board positions
UniquePosRecord *hashTable = NULL;


// additional memory to store positions in case there is hash collision
// 200 million entries for extra alloc (10000 MB)
#define EXTRA_ALLOC_SIZE 200 * 1024 * 1024
UniquePosRecord *additionalAlloc;
uint32 indexInAlloc = 0;

uint32 allocNewRecord(UniquePosRecord *prev)
{
    if (additionalAlloc == NULL)
    {
        printf("\nAllocating additional memory\n");
        additionalAlloc = (UniquePosRecord *) malloc(EXTRA_ALLOC_SIZE * sizeof(UniquePosRecord));
        memset(additionalAlloc, 0, EXTRA_ALLOC_SIZE * sizeof(UniquePosRecord));
    }

    UniquePosRecord *newEntry = &additionalAlloc[indexInAlloc];
    if (indexInAlloc == EXTRA_ALLOC_SIZE)
    {
        printf("\nRan out of memory!!\n");
        getch();
        exit(0);
    }

    // add to chain and return the entry
    prev->next = indexInAlloc;
    return indexInAlloc++;
}

bool findPositionAndUpdateCounter(HexaBitBoardPosition *pos, uint64 hash, uint32 partialCount)
{
    if (hashTable == NULL)
    {
        hashTable = (UniquePosRecord*)malloc(UNIQUE_TABLE_SIZE * sizeof(UniquePosRecord));
        memset(hashTable, 0, UNIQUE_TABLE_SIZE * sizeof(UniquePosRecord));
    }

    UniquePosRecord *record = &hashTable[hash & UNIQUE_TABLE_INDEX_BITS];
    while (true)
    {
        if (record->hash == hash)
        {
            // match
            record->count += partialCount;
            return true;
        }
        else if (record->hash == 0)
        {
            // empty
            QuadBitBoardPosition qbb;
            GameState state;
            HexaToQuadBB(&qbb, &state, pos);

            record->pos = qbb;
            record->state = state;
            record->hash = hash;
            record->count = partialCount;
            record->next = ~0;
            return false;
        }

        uint32 nextRecord = record->next;
        if (nextRecord == ~0)
        {
            nextRecord = allocNewRecord(record);
        }
        record = &additionalAlloc[nextRecord];
    }

}

// find uniques till specified depth
uint64 perft_unique(HexaBitBoardPosition *pos, uint32 depth, uint32 partialCount = 1)
{
    CMove moves[MAX_MOVES];
    uint32 nMoves = 0;

    if (depth == 0)
    {
        // check the postion in list of existing positions
        // add to list (with occurence count = 1) if it's a new position
        // otherwise just increment the occurence counter of the position
        uint64 hash = computeZobristKey(pos);
        bool found = findPositionAndUpdateCounter(pos, hash, partialCount);

        if (found)
            return 0;
        else
            return 1;
    }

    nMoves = generateMoves(pos, moves);

    uint64 uniqueCount = 0;
    for (int i = 0; i < nMoves; i++)
    {
        HexaBitBoardPosition childPos = *pos;
        uint64 fakeHash = 0;
        makeMove(&childPos, fakeHash, moves[i], pos->chance);
        uniqueCount += perft_unique(&childPos, depth - 1, partialCount);
    }

    return uniqueCount;
}

void saveUniquesToFile(int depth)
{
    char fileName[256];
    sprintf(fileName, "c:\\ankan\\unique\\uniques_%d.dat", depth);

    FILE *fp = fopen(fileName, "wb+");

    // TODO: sort before writing?

    int recordsWritten = 0;
    // 1. first save hash table entries
    for (int i = 0; i < UNIQUE_TABLE_SIZE; i++)
    {
        UniquePosRecord *record = &hashTable[i];
        if (record->count)
        {
            FileRecord rec;
            rec.pos = record->pos;
            rec.count = record->count;
            rec.chance = record->state.chance;
            rec.enPassent = record->state.enPassent;
            rec.whiteCastle = record->state.whiteCastle;
            rec.blackCastle = record->state.blackCastle;

            fwrite(&rec, sizeof(FileRecord), 1, fp);
            recordsWritten++;
        }
    }

    // 2. save the entries in additional allocation
    for (int i = 0; i < indexInAlloc; i++)
    {
        UniquePosRecord *record = &additionalAlloc[i];
        FileRecord rec;
        rec.pos = record->pos;
        rec.count = record->count;
        rec.chance = record->state.chance;
        rec.enPassent = record->state.enPassent;
        rec.whiteCastle = record->state.whiteCastle;
        rec.blackCastle = record->state.blackCastle;

        fwrite(&rec, sizeof(FileRecord), 1, fp);
        recordsWritten++;
    }

    fclose(fp);
    printf("\n%d records saved\n", recordsWritten);

    // delete the hash table and additional allocation
    free(hashTable);
    hashTable = NULL;
    free(additionalAlloc);
    additionalAlloc = NULL;
    indexInAlloc = 0;
}



// find unique chess positions (and their occurence counts) for the specified depth
// save them in a binary file (that can be later used to compute deeper perfts
// the file is sorted on the hash of position to make merging (and duplicate removal) easy 
// when processing multiple files later
void findUniques(int depth)
{
    BoardPosition testBoard;
    Utils::readFENString("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &testBoard);
    Utils::dispBoard(&testBoard);
    HexaBitBoardPosition testBB;
    Utils::board088ToHexBB(&testBB, &testBoard);

    start = clock();
    uint64 count = perft_unique(&testBB, depth);
    end = clock();

    double t = ((double)end - start) / CLOCKS_PER_SEC;

    printf("\nUnique(%d) = %llu, time: %g seconds\n", depth, count, t);

    // save to disk
    saveUniquesToFile(depth);

    // compute further unique values using the results from previous level
    int curDepth = depth + 1;
    while (1)
    {
        // read from file into memory 
        // and then find child boards and uniques for them
        char fileName[256];
        sprintf(fileName, "c:\\ankan\\unique\\uniques_%d.dat", curDepth - 1);
        FILE *fp = fopen(fileName, "rb+");

        while(1)
        {
            FileRecord record;
            int read = fread(&record, sizeof(FileRecord), 1, fp);
            if (!read)
            {
                // done!
                break;
            }

            // TODO: handle when we run out of memory
            // run sort + merge algorithm to do sorted merging and duplicate removal

            HexaBitBoardPosition pos;
            GameState state = { 0 };
            state.blackCastle = record.blackCastle;
            state.chance = record.chance;
            state.enPassent = record.enPassent;
            state.whiteCastle = record.whiteCastle;

            quadToHexaBB(&pos, &(record.pos), &state);
            perft_unique(&pos, 1, record.count);
        }
        fclose(fp);


        saveUniquesToFile(curDepth);
        curDepth++;
    }

    getchar();
}