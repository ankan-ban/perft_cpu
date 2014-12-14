
//#include "chess.h"
#include "MoveGenerator088.h"
#include "MoveGeneratorBitboard.h"

#include <windows.h>

// for timing CPU code : start
double gTime;
LARGE_INTEGER freq;
#define START_TIMER { \
    LARGE_INTEGER count1, count2; \
    QueryPerformanceFrequency (&freq);  \
    QueryPerformanceCounter(&count1);

#define STOP_TIMER \
    QueryPerformanceCounter(&count2); \
    gTime = ((double)(count2.QuadPart-count1.QuadPart)*1000.0)/freq.QuadPart; \
    }
// for timing CPU code : end

void removeNewLine(char *str)
{
    while (*str)
    {
        if (*str == '\n' || *str == '\r')
        {
            *str = 0;
            break;
        }
        str++;
    }
}


// a complete work unit is read and written at once (not very big... ~50 MB max)
#define MAX_THREADS 1024
#define MAX_RECORDS 100000
#define MAX_RECORD_SIZE 256

struct perft14wu
{
    char input[MAX_RECORDS][MAX_RECORD_SIZE];
    char output[MAX_RECORDS][MAX_RECORD_SIZE];

    volatile unsigned int nextRecord;
    unsigned int totalRecords;
    unsigned int preProcessedRecords;

    // most recent record processed by each thread
    volatile int mostRecentProcessed[MAX_THREADS];
} g_WorkUnit;

clock_t start, end;

DWORD WINAPI workerThread(LPVOID lpParam)
{
    BoardPosition testBoard;
    int threadIndex = (int) lpParam;
    
    while (true)
    {
        int recordIdToProcess = InterlockedIncrement(&g_WorkUnit.nextRecord);

        // done ?
        if (recordIdToProcess >= g_WorkUnit.totalRecords)
            break;

        char *line = g_WorkUnit.input[recordIdToProcess];

        Utils::readFENString(line, &testBoard);
        HexaBitBoardPosition testBB;

        //Utils::dispBoard(&testBoard);


        Utils::board088ToHexBB(&testBB, &testBoard);

        uint64 res = perft_bb(&testBB, 0, 7);

        // write to output file
        removeNewLine(line);

        // parse the occurence count (last number in the line)
        char *ptr = line;
        while (*ptr) ptr++;
        while (*ptr != ' ') ptr--;
        int occCount = atoi(ptr);

        sprintf(g_WorkUnit.output[recordIdToProcess], "%s %llu %llu\n", line, res, res * occCount);

        end = clock();
        double t = ((double)end - start) / CLOCKS_PER_SEC;
        printf("\nTID: %d: record id: %d\n%sRecords done: %d, Total: %g seconds, Avg: %g seconds\n", threadIndex, recordIdToProcess,
                    g_WorkUnit.output[recordIdToProcess], 
                    recordIdToProcess + g_WorkUnit.preProcessedRecords + 1,
                    t, t / (recordIdToProcess+1));

        fflush(stdout);

        g_WorkUnit.mostRecentProcessed[threadIndex] = recordIdToProcess;

    }

    return 0;
}


int main(int argc, char *argv[])
{
    BoardPosition testBoard;
    MoveGeneratorBitboard::init();

    if (argc >= 2)
    {
        // perft verification mode

        FILE *fpInp;    // input file
        FILE *fpOp;     // output file
        int startRecord = 0;

        char opFile[1024];
        sprintf(opFile, "%s.op", argv[1]);
        printf("filename of op: %s", opFile);

        fpInp = fopen(argv[1], "rb+");
        fpOp = fopen(opFile, "ab+");

        fseek(fpOp, 0, SEEK_SET);

        start = clock();

        char line[MAX_RECORD_SIZE];

        int j = 0;  // records to process
        int k = 0;  // already processed records

        printf("\nReading input file...\n");
        while (fgets(line, MAX_RECORD_SIZE, fpInp))
        {
            if (fgets(opFile, MAX_RECORD_SIZE, fpOp))
            {
                k++;
                // skip already processed records
                continue;
            }
            strcpy(g_WorkUnit.input[j], line);
            j++;
        }
        fclose(fpInp);

        g_WorkUnit.totalRecords = j;
        g_WorkUnit.nextRecord = -1;    // first interlockedIncrement will return incremented value
        g_WorkUnit.preProcessedRecords = k;

        printf("\nTotal: %d, processed: %d\n", g_WorkUnit.totalRecords, g_WorkUnit.preProcessedRecords);

        // create multiple threads to distribute the work
        HANDLE childThreads[MAX_THREADS];
        int numThreads = 7;
        if (argc >= 3)
            numThreads = atoi(argv[2]);
        if (numThreads < 1 || numThreads > MAX_THREADS)
            numThreads = 7;

        printf("\nlaunching %d threads...\n", numThreads);
        for (int i = 0; i < numThreads; i++)
        {
            childThreads[i] = CreateThread(NULL, 0, workerThread, (LPVOID)i, 0, NULL);
        }

        // Wait until all threads have terminated.
        printf("\nWaiting for child threads to finish...\n");

        int lastRecordWritten = 0;
        while (WaitForMultipleObjects(numThreads, childThreads, TRUE, /*INFINITE*/ 1000) == WAIT_TIMEOUT)
        {
            // write output records every 1 second

            // figure out lowest completed index
            int minIndex = g_WorkUnit.totalRecords;
            for (int i = 0; i < numThreads; i++)
            {
                int threadRecent = g_WorkUnit.mostRecentProcessed[i];
                if (threadRecent < minIndex)
                    minIndex = threadRecent;
            }

            for (int i = lastRecordWritten; i < minIndex; i++)
            {
                // printf("\nWriting record: %d\n", i);
                fprintf(fpOp, "%s", g_WorkUnit.output[i]);
                fflush(fpOp);
            }

            lastRecordWritten = minIndex;
        }

        // write remaining records
        for (int i = lastRecordWritten; i < g_WorkUnit.totalRecords; i++)
        {
            fprintf(fpOp, "%s", g_WorkUnit.output[i]);
            fflush(fpOp);
        }

        fclose(fpOp);
        return 0;
    }

    // some test board positions from http://chessprogramming.wikispaces.com/Perft+Results

    // no bug bug till depth 7
    Utils::readFENString("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &testBoard); // start.. 20 positions

    // No bug till depth 6!
    //Utils::readFENString("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", &testBoard); // position 2 (caught max bugs for me)

    // No bug till depth 7!
    //Utils::readFENString("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", &testBoard); // position 3

    // no bug till depth 6
    //Utils::readFENString("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", &testBoard); // position 4
    //Utils::readFENString("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", &testBoard); // mirror of position 4
    
    // no bug till depth 6!
    //Utils::readFENString("rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6", &testBoard);   // position 5

    // no bug till depth 7
    //Utils::readFENString("3Q4/1Q4Q1/4Q3/2Q4R/Q4Q2/3Q4/1Q4Rp/1K1BBNNk w - - 0 1", &testBoard); // - 218 positions.. correct!

    // no bug till depth 10
    //Utils::readFENString("3k4/8/8/K1Pp3r/8/8/8/8 w - d6 0 1", &testBoard);

    // bug at depth 11!! (expected is 337,294,265,604 - but we currently get 277,164,723,460)
    // TODO: fix this bug!
    // problem is likely with hash key collision, perft_gpu (without hash) gives correct result:
    // GPU Perft 11: 337294265604,   Time taken: 48.6369 seconds, nps: 6934942128
    //Utils::readFENString("RNBKQBNR/PPPPPPPP/8/8/8/8/pppppppp/rnbkqbnr w - -", &testBoard);


    /*
    HexaBitBoardPosition newMoves[MAX_MOVES];
    uint32 bbMoves = MoveGeneratorBitboard::generateMoves(&testBB, newMoves);
    */    
    
    //findMagics();

    printf("\nEnter FEN String: \n");
    char fen[1024];
    gets(fen);
    Utils::readFENString(fen, &testBoard); 
    Utils::dispBoard(&testBoard);

    HexaBitBoardPosition testBB;
    Utils::board088ToHexBB(&testBB, &testBoard);


    uint64 zobristHash = 0;
#if INCREMENTAL_ZOBRIST_UPDATE == 1
    // compute hash of the position
    zobristHash = computeZobristKey(&testBB);
#endif

    int minDepth = 1;
    int maxDepth = 32;

    uint64 bbMoves;

    for (int depth=minDepth;depth<=maxDepth;depth++)
    {
#if DEBUG_PRINT_MOVES == 1
        int depth = DEBUG_PRINT_DEPTH;
#endif
#if DEBUG_PRINT_UNIQUE_COUNTMOVES == 1        
        globalCountMovesCounter = 0;
#endif

#if PRINT_HASH_STATS == 1
        for (int i=0;i<=depth;i++)
        {
            numProbes[i] = 0;
            numHits[i] = 0;
            numStores[i] = 0;
        }
#endif

        START_TIMER
        bbMoves = perft_bb(&testBB, zobristHash, depth);
        STOP_TIMER
        printf("\nPerft %d: %llu,   ", depth, bbMoves);
        printf("Time taken: %g seconds, nps: %llu\n", gTime/1000.0, (uint64) ((bbMoves/gTime)*1000.0));

#if DEBUG_PRINT_UNIQUE_COUNTMOVES == 1        
        printf("No of calls to countMoves: %llu\n", globalCountMovesCounter);
#endif

#if PRINT_HASH_STATS == 1
    printf("\nHash stats per depth\n");
    printf("depth   hash probes      hash hits    hash stores\n");
    for (int i=2; i<=depth; i++)
        printf("%5d   %11llu    %11llu    %11llu\n", i, numProbes[i], numHits[i], numStores[i]);
#endif

#if DEBUG_PRINT_TIME_BREAKUP == 1        
        printf("zobrist computation time: %g seconds, countMoves: %g seconds, makeMove: %g seconds\n", 
            ((double) total_time_in_zob.QuadPart)        / freq.QuadPart ,
            ((double) total_time_in_countMoves.QuadPart) / freq.QuadPart ,
            ((double) total_time_in_makeMove.QuadPart)   / freq.QuadPart);
#endif
    }
    
    MoveGeneratorBitboard::destroy();
    return 0;
}