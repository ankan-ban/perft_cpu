
//#include "chess.h"
#include "MoveGenerator088.h"
#include "MoveGeneratorBitboard.h"



// for timing CPU code : start
double gTime;
#define START_TIMER { \
    LARGE_INTEGER count1, count2, freq; \
    QueryPerformanceFrequency (&freq);  \
    QueryPerformanceCounter(&count1);

#define STOP_TIMER \
    QueryPerformanceCounter(&count2); \
    gTime = ((double)(count2.QuadPart-count1.QuadPart)*1000.0)/freq.QuadPart; \
    }
// for timing CPU code : end


// perft counter function. Returns perft of the given board for given depth
uint64 perft_bb(HexaBitBoardPosition *pos, uint32 depth)
{
    HexaBitBoardPosition newPositions[256];

    /*
    if (depth == 2)
        printMoves = true;
    else
        printMoves = false;
    */

    uint32 nMoves = 0;
    uint8 chance = pos->chance;

    if (depth == 1)
    {
#if USE_TEMPLATE_CHANCE_OPT == 1
    if (chance == BLACK)
    {
        nMoves = MoveGeneratorBitboard::generateMoves<BLACK, true>(pos, newPositions);
    }
    else
    {
        nMoves = MoveGeneratorBitboard::generateMoves<WHITE, true>(pos, newPositions);
    }
#else
    nMoves = MoveGeneratorBitboard::generateMoves(pos, newPositions, chance, true);
#endif
        return nMoves;
    }

#if USE_TEMPLATE_CHANCE_OPT == 1
    if (chance == BLACK)
    {
        nMoves = MoveGeneratorBitboard::generateMoves<BLACK, false>(pos, newPositions);
    }
    else
    {
        nMoves = MoveGeneratorBitboard::generateMoves<WHITE, false>(pos, newPositions);
    }
#else
    nMoves = MoveGeneratorBitboard::generateMoves(pos, newPositions, chance, false);
#endif

    uint64 count = 0;

    for (uint32 i=0; i < nMoves; i++)
    {
        uint64 childPerft = perft_bb(&newPositions[i], depth - 1);
        /*if (depth == 2)
            printf("%llu\n", childPerft);*/
        count += childPerft;
    }

    return count;
}



int main()
{
    BoardPosition testBoard;

    MoveGeneratorBitboard::init();

    // some test board positions from http://chessprogramming.wikispaces.com/Perft+Results

    // no bug bug till depth 7
    //Utils::readFENString("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &testBoard); // start.. 20 positions

    // No bug till depth 6!
    Utils::readFENString("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", &testBoard); // position 2 (caught max bugs for me)

    // No bug till depth 7!
    // Utils::readFENString("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", &testBoard); // position 3

    // no bug till depth 6
    //Utils::readFENString("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", &testBoard); // position 4
    //Utils::readFENString("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", &testBoard); // mirror of position 4
    
    // no bug till depth 6!
    //Utils::readFENString("rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6", &testBoard);   // position 5

    // no bug till depth 7
    //Utils::readFENString("3Q4/1Q4Q1/4Q3/2Q4R/Q4Q2/3Q4/1Q4Rp/1K1BBNNk w - - 0 1", &testBoard); // - 218 positions.. correct!

    //Utils::readFENString("rnb1kb1r/ppqp1ppp/2p5/4P3/2B5/6K1/PPP1N1PP/RNBQ3R b kq - 0 6", &testBoard); // temp test


    /*
    // bug!
    printf("\nsquares between: %llu\n", MoveGeneratorBitboard::squaresInBetween(G8, B3));
    printf("\nsquares between: %llu\n", MoveGeneratorBitboard::squaresInBetween(B3, G8));

    HexaBitBoardPosition newMoves[MAX_MOVES];
    uint32 bbMoves = MoveGeneratorBitboard::generateMoves(&testBB, newMoves);
    */
    
    printf("\nEnter FEN String: \n");
    char fen[1024];
    gets(fen);
    Utils::readFENString(fen, &testBoard); // start.. 20 positions
    Utils::dispBoard(&testBoard);

    HexaBitBoardPosition testBB;
    Utils::board088ToHexBB(&testBB, &testBoard);
    Utils::boardHexBBTo088(&testBoard, &testBB);


    uint64 bbMoves;

    for (int depth=1;depth<9;depth++)
    {
        //int depth = 5;
        START_TIMER
        bbMoves = perft_bb(&testBB, depth);
        STOP_TIMER
        printf("\nPerft %d: %llu,   ", depth, bbMoves);
        printf("Time taken: %g seconds, nps: %llu\n", gTime/1000.0, (uint64) ((bbMoves/gTime)*1000.0));
    }
    
    return 0;
}