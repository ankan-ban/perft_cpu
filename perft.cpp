
//#include "chess.h"
#include "MoveGenerator088.h"
#include "MoveGeneratorBitboard.h"

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



int main()
{
    BoardPosition testBoard;

    MoveGeneratorBitboard::init();

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
    Utils::readFENString(fen, &testBoard); // start.. 20 positions
    Utils::dispBoard(&testBoard);

    HexaBitBoardPosition testBB;
    Utils::board088ToHexBB(&testBB, &testBoard);


#if INCREMENTAL_ZOBRIST_UPDATE == 1
    // compute hash of the position
    testBB.zobristHash = computeZobristKey(&testBB);
#endif

    int minDepth = 1;
    int maxDepth = 20;

    uint64 bbMoves;

    for (int depth=minDepth;depth<=maxDepth;depth++)
    {
#if DEBUG_PRINT_MOVES == 1
        int depth = DEBUG_PRINT_DEPTH;
#endif
#if DEBUG_PRINT_UNIQUE_COUNTMOVES == 1        
        globalCountMovesCounter = 0;
#endif
        START_TIMER
        bbMoves = perft_bb(&testBB, depth);
        STOP_TIMER
        printf("\nPerft %d: %llu,   ", depth, bbMoves);
        printf("Time taken: %g seconds, nps: %llu\n", gTime/1000.0, (uint64) ((bbMoves/gTime)*1000.0));

#if DEBUG_PRINT_UNIQUE_COUNTMOVES == 1        
        printf("No of calls to countMoves: %llu\n", globalCountMovesCounter);
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