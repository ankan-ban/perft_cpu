#include "chess.h"

// 128-bit hash keys for deep-perfts
union HashKey128b
{
    struct
    {
        uint64 lowPart;
        uint64 highPart;
    };

    HashKey128b() { lowPart = highPart = 0ull; }
    HashKey128b(uint64 l, uint64 h) { lowPart = l, highPart = h; }

    HashKey128b operator^(const HashKey128b& b)
    {
        HashKey128b temp;
        temp.lowPart = this->lowPart ^ b.lowPart;
        temp.highPart = this->highPart ^ b.highPart;
        return temp;
    }

    HashKey128b operator^=(const HashKey128b& b)
    {
        this->lowPart = this->lowPart ^ b.lowPart;
        this->highPart = this->highPart ^ b.highPart;
        return *this;
    }

    bool operator==(const HashKey128b& b)
    {
        return (this->highPart == b.highPart) && (this->lowPart == b.lowPart);
    }
};
CT_ASSERT(sizeof(HashKey128b) == 16);


#define ZOB_KEY1(x) (zob.x)
#define ZOB_KEY2(x) (zob2.x)
#define ZOB_KEY_128(x) (HashKey128b(ZOB_KEY1(x), ZOB_KEY2(x)))

// compute zobrist hash key for a given board position (128 bit hash)
HashKey128b computeZobristKey128b(HexaBitBoardPosition *pos)
{
    HashKey128b key(0, 0);

    // chance (side to move)
    if (pos->chance == WHITE)
        key ^= ZOB_KEY_128(chance);

    // castling rights
    if (pos->whiteCastle & CASTLE_FLAG_KING_SIDE)
        key ^= ZOB_KEY_128(castlingRights[WHITE][0]);
    if (pos->whiteCastle & CASTLE_FLAG_QUEEN_SIDE)
        key ^= ZOB_KEY_128(castlingRights[WHITE][1]);

    if (pos->blackCastle & CASTLE_FLAG_KING_SIDE)
        key ^= ZOB_KEY_128(castlingRights[BLACK][0]);
    if (pos->blackCastle & CASTLE_FLAG_QUEEN_SIDE)
        key ^= ZOB_KEY_128(castlingRights[BLACK][1]);


    // en-passent target
    if (pos->enPassent)
    {
        key ^= ZOB_KEY_128(enPassentTarget[pos->enPassent - 1]);
    }


    // piece-position
    uint64 allPawns = pos->pawns & RANKS2TO7;    // get rid of game state variables
    uint64 allPieces = pos->kings | allPawns | pos->knights | pos->bishopQueens | pos->rookQueens;

    while (allPieces)
    {
        uint64 piece = MoveGeneratorBitboard::getOne(allPieces);
        int square = bitScan(piece);

        int color = !(piece & pos->whitePieces);
        if (piece & allPawns)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_PAWN][square]);
        }
        else if (piece & pos->kings)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_KING][square]);
        }
        else if (piece & pos->knights)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_KNIGHT][square]);
        }
        else if (piece & pos->rookQueens & pos->bishopQueens)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_QUEEN][square]);
        }
        else if (piece & pos->rookQueens)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_ROOK][square]);
        }
        else if (piece & pos->bishopQueens)
        {
            key ^= ZOB_KEY_128(pieces[color][ZOB_INDEX_BISHOP][square]);
        }

        allPieces ^= piece;
    }

    return key;
}

struct UniquePosRecord
{
    HashKey128b             hash;    // 16 bytes
    HexaBitBoardPosition    pos;     // 48 bytes
    uint64                  count;   //  8 bytes
    UniquePosRecord*        next;    //  8 bytes pointer to next record (for chaining)
};
CT_ASSERT(sizeof(UniquePosRecord) == 80);

// 26 bits -> 64 million entries: 5120 MB hash table
#define UNIQUE_TABLE_BITS           26
#define UNIQUE_TABLE_SIZE           (1 << UNIQUE_TABLE_BITS)
#define UNIQUE_TABLE_INDEX_BITS     (UNIQUE_TABLE_SIZE - 1)
#define UNIQUE_TABLE_HASH_BITS      (ALLSET ^ UNIQUE_TABLE_INDEX_BITS)

// hash table to store unique board positions
UniquePosRecord *hashTable = NULL;


// additional memory to store positions in case there is hash collision
#define MAX_EXTRA_ALLOCS 10
// 16 million entries for each extra alloc
#define EXTRA_ALLOC_SIZE 16 * 1024 * 1024
UniquePosRecord *additionalAllocs[MAX_EXTRA_ALLOCS];
uint32 currentAlloc = 0;
uint32 indexInCurrent = 0;

UniquePosRecord *allocNewRecord(UniquePosRecord *prev)
{
    if (additionalAllocs[currentAlloc] == NULL)
    {
        printf("\nAllocating additional memory index %d\n", currentAlloc);
        additionalAllocs[currentAlloc] = (UniquePosRecord *) malloc(EXTRA_ALLOC_SIZE * sizeof(UniquePosRecord));
        memset(additionalAllocs[currentAlloc], 0, EXTRA_ALLOC_SIZE * sizeof(UniquePosRecord));
    }

    UniquePosRecord *newEntry = &additionalAllocs[currentAlloc][indexInCurrent++];
    if (indexInCurrent == EXTRA_ALLOC_SIZE)
    {
        currentAlloc++;
        indexInCurrent = 0;
        if (currentAlloc == MAX_EXTRA_ALLOCS)
        {
            printf("\nRan out of memory!!\n");
            getch();
            exit(0);
        }
    }

    // add to chain and return the entry
    prev->next = newEntry;
    return newEntry;
}

bool findPositionAndUpdateCounter(HexaBitBoardPosition *pos, HashKey128b hash, uint32 partialCount)
{
    if (hashTable == NULL)
    {
        hashTable = (UniquePosRecord*)malloc(UNIQUE_TABLE_SIZE * sizeof(UniquePosRecord));
        memset(hashTable, 0, UNIQUE_TABLE_SIZE * sizeof(UniquePosRecord));
    }

    UniquePosRecord *record = &hashTable[hash.lowPart & UNIQUE_TABLE_INDEX_BITS];
    while (true)
    {
        if (record->hash == hash)
        {
            // match
            record->count += partialCount;
            return true;
        }
        else if (record->hash == HashKey128b(0, 0))
        {
            // empty
            record->pos = *pos;
            record->hash = hash;
            record->count = partialCount;
            record->next = NULL;
            return false;
        }

        UniquePosRecord *nextRecord = record->next;
        if (nextRecord == NULL)
        {
            nextRecord = allocNewRecord(record);
        }
        record = nextRecord;
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
        HashKey128b hash = computeZobristKey128b(pos);
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

    int recordsWritten = 0;
    // 1. first save hash table entries
    for (int i = 0; i < UNIQUE_TABLE_SIZE; i++)
    {
        UniquePosRecord *record = &hashTable[i];
        if (record->count)
        {
            fwrite(record, sizeof(UniquePosRecord), 1, fp);
            recordsWritten++;
        }
    }

    // 2. now save completely filled allocation entries
    for (int i = 0; i < currentAlloc; i++)
    {
        fwrite(additionalAllocs[i], sizeof(UniquePosRecord), EXTRA_ALLOC_SIZE, fp);
        recordsWritten += EXTRA_ALLOC_SIZE;
    }

    // 3. finally save the last partial additional allocation
    if (indexInCurrent)
    {
        fwrite(additionalAllocs[currentAlloc], sizeof(UniquePosRecord), indexInCurrent, fp);
        recordsWritten += indexInCurrent;
    }
    fclose(fp);
    printf("\n%d records saved\n", recordsWritten);

    // delete the hash table and additional allocations
    free(hashTable);
    hashTable = NULL;

    for (int i = 0; i < MAX_EXTRA_ALLOCS; i++)
    {
        if (additionalAllocs[i])
        {
            free(additionalAllocs[i]);
            additionalAllocs[i] = NULL;
        }
    }
    currentAlloc = 0;
    indexInCurrent = 0;
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

    const int tempAllocSize = 10 * 1024 * 1024;
    UniquePosRecord* tempAlloc = (UniquePosRecord*)malloc(sizeof(UniquePosRecord)*tempAllocSize);

    // compute further unique values using the results from previous level
    int curDepth = depth + 1;
    while (1)
    {
        // read from file into memory (upto 16 million records a time)
        // and then find child boards and uniques for them
        char fileName[256];
        sprintf(fileName, "c:\\ankan\\unique\\uniques_%d.dat", curDepth - 1);
        FILE *fp = fopen(fileName, "rb+");
        int read = fread(tempAlloc, sizeof(UniquePosRecord), tempAllocSize, fp);
        printf("\n%d records read\n", read);
        fclose(fp);

        for (int i = 0; i < read; i++)
        {
            perft_unique(&(tempAlloc[i].pos), 1, tempAlloc[i].count);
        }

        if (read < tempAllocSize)
        {
            saveUniquesToFile(curDepth);
            curDepth++;
        }
        else
        {
            // break into multiple parts
            // step 1: sort the list (based on hash)
            // first save from hash table to disk and read back to a linear temp allocation

            printf("\nFile too big\n");
            break;
        }
    }


    free(tempAlloc);
    getchar();
}