/****************************************************************************
 *  Compress chess moves into one byte
 *  Author:  Bill Forster
 *  License: MIT license. Full text of license is in associated file LICENSE
 *  Copyright 2010-2014, Bill Forster <billforsternz at gmail dot com>
 ****************************************************************************/

#include "CompressMoves.h"

// Compression method is basically hi nibble indicates one of 16 pieces, lo nibble indicates how piece moves
#define CODE_SAME_FILE                          8          // Rank or file codes, this bit set indicates same file (so remaining 3 bits encode rank)
#define CODE_FALL                               8          // Diagonal codes, either RISE/ or FALL\, other 3 bits encode destination file
#define CODE_SPECIAL_PROMOTION_QUEEN            0x00       // Pawn codes, Lo=2 bit promotion + 2 bit direction
#define CODE_SPECIAL_PROMOTION_ROOK             0x04       //
#define CODE_SPECIAL_PROMOTION_BISHOP           0x08       //
#define CODE_SPECIAL_PROMOTION_KNIGHT           0x0c       //

#define CODE_K_SPECIAL_WK_CASTLING              0x01       // King codes, Hi=0, Lo= non-zero vector or castling type
#define CODE_K_SPECIAL_BK_CASTLING              0x02       //
#define CODE_K_SPECIAL_WQ_CASTLING              0x03       //
#define CODE_K_SPECIAL_BQ_CASTLING              0x04       //
#define CODE_K_VECTOR_0                         0x05       //
#define CODE_K_VECTOR_1                         0x06
#define CODE_K_VECTOR_2                         0x07
#define CODE_K_VECTOR_3                         0x08
#define CODE_K_VECTOR_4                         0x09
#define CODE_K_VECTOR_5                         0x0b            // don't assign 0x0a = '\n', to make string more like a 'line' of text
#define CODE_K_VECTOR_6                         0x0e            // don't assign 0x0d = '\r', to make string more like a 'line' of text
#define CODE_K_VECTOR_7                         0x0f

#define CODE_N_SHADOW                           8          // Knight codes, this bit indicates knight code is a shadowed rank or file
#define CODE_N_VECTOR_0                         0x00       // If CODE_N_SHADOW bit is low, then 8 codes for all N vectors
#define CODE_N_VECTOR_1                         0x01       //
#define CODE_N_VECTOR_2                         0x02
#define CODE_N_VECTOR_3                         0x03
#define CODE_N_VECTOR_4                         0x04
#define CODE_N_VECTOR_5                         0x05
#define CODE_N_VECTOR_6                         0x06
#define CODE_N_VECTOR_7                         0x07

// Square macros
#define FILE(sq)    ( (char) (  ((sq)&0x07) + 'a' ) )               // eg c5->'c'
#define RANK(sq)    ( (char) (  '8' - (((sq)>>3) & 0x07) ) )        // eg c5->'5'
#define SOUTH(sq)   (  (thc::Square)((sq) + 8) )                    // eg c5->c4
#define NORTH(sq)   (  (thc::Square)((sq) - 8) )                    // eg c5->c6

std::string SqToStr( int sq )
{
    char file = FILE(sq);
    char rank = RANK(sq);
    char buf[3];
    buf[0] = file;
    buf[1] = rank;
    buf[2] = '\0';
    std::string s = buf;
    return s;
}

std::string SqToStr( thc::Square sq )
{
    return SqToStr( (int)sq );
}

CompressMoves::CompressMoves( const CompressMoves& copy_from_me )
{
    *this = copy_from_me;
}

CompressMoves & CompressMoves::operator= (const CompressMoves & copy_from_me )
{
    cr = copy_from_me.cr;
    for( int i=0; i<16; i++ )
    {
        white_pieces[i] = copy_from_me.white_pieces[i];
        if( copy_from_me.white_pieces[i].shadow_file )
            white_pieces[i].shadow_file = white_pieces + (copy_from_me.white_pieces[i].shadow_file - copy_from_me.white_pieces);
        if( copy_from_me.white_pieces[i].shadow_rank )
            white_pieces[i].shadow_rank = white_pieces + (copy_from_me.white_pieces[i].shadow_rank - copy_from_me.white_pieces);
        if( copy_from_me.white_pieces[i].shadow_rook )
            white_pieces[i].shadow_rook = white_pieces + (copy_from_me.white_pieces[i].shadow_rook - copy_from_me.white_pieces);
        if( copy_from_me.white_pieces[i].shadow_owner )
            white_pieces[i].shadow_owner = white_pieces + (copy_from_me.white_pieces[i].shadow_owner - copy_from_me.white_pieces);
    }
    for( int i=0; i<16; i++ )
    {
        black_pieces[i] = copy_from_me.black_pieces[i];
        if( copy_from_me.black_pieces[i].shadow_file )
            black_pieces[i].shadow_file = black_pieces + (copy_from_me.black_pieces[i].shadow_file - copy_from_me.black_pieces);
        if( copy_from_me.black_pieces[i].shadow_rank )
            black_pieces[i].shadow_rank = black_pieces + (copy_from_me.black_pieces[i].shadow_rank - copy_from_me.black_pieces);
        if( copy_from_me.black_pieces[i].shadow_rook )
            black_pieces[i].shadow_rook = black_pieces + (copy_from_me.black_pieces[i].shadow_rook - copy_from_me.black_pieces);
        if( copy_from_me.black_pieces[i].shadow_owner )
            black_pieces[i].shadow_owner = black_pieces + (copy_from_me.black_pieces[i].shadow_owner - copy_from_me.black_pieces);
    }
    for( int i=0; i<64; i++ )
    {
        Tracker *p = copy_from_me.trackers[64];
        Tracker *q=0;
        if( p )
        {
            if( copy_from_me.white_pieces <= p  &&  p < &copy_from_me.white_pieces[16] )
                q = white_pieces + (p-copy_from_me.white_pieces);
            else if( copy_from_me.black_pieces <= p  &&  p < &copy_from_me.black_pieces[16] )
                q = black_pieces + (p-copy_from_me.black_pieces);
        }
        trackers[i] = q;
    }
    return *this;
}

void CompressMoves::Init()
{
    cr.Init();
    ((thc::ChessPosition)cr).Init();
    for( int i=0; i<64; i++ )
        trackers[i] = NULL;
    square_init( TI_K,  'K', thc::e1 );
    square_init( TI_KN, 'N', thc::g1 );
    square_init( TI_QN, 'N', thc::b1 );
    square_init( TI_KR, 'R', thc::h1 );
    square_init( TI_QR, 'R', thc::a1 );
    square_init( TI_KB, 'B', thc::f1 );
    square_init( TI_QB, 'B', thc::c1 );
    square_init( TI_Q,  'Q', thc::d1 );
    white_pieces[TI_Q].shadow_rank   = &white_pieces[TI_KN];
    white_pieces[TI_KN].shadow_owner = &white_pieces[TI_Q];
    white_pieces[TI_Q].shadow_file   = &white_pieces[TI_QN];
    white_pieces[TI_QN].shadow_owner = &white_pieces[TI_Q];
    square_init( TI_AP, 'P', thc::a2 );
    square_init( TI_BP, 'P', thc::b2 );
    square_init( TI_CP, 'P', thc::c2 );
    square_init( TI_DP, 'P', thc::d2 );
    square_init( TI_EP, 'P', thc::e2 );
    square_init( TI_FP, 'P', thc::f2 );
    square_init( TI_GP, 'P', thc::g2 );
    square_init( TI_HP, 'P', thc::h2 );
    square_init( TI_K,  'k', thc::e8 );
    square_init( TI_KN, 'n', thc::g8 );
    square_init( TI_QN, 'n', thc::b8 );
    square_init( TI_KR, 'r', thc::h8 );
    square_init( TI_QR, 'r', thc::a8 );
    square_init( TI_KB, 'b', thc::f8 );
    square_init( TI_QB, 'b', thc::c8 );
    square_init( TI_Q,  'q', thc::d8 );
    black_pieces[TI_Q].shadow_rank   = &black_pieces[TI_KN];
    black_pieces[TI_KN].shadow_owner = &black_pieces[TI_Q];
    black_pieces[TI_Q].shadow_file   = &black_pieces[TI_QN];
    black_pieces[TI_QN].shadow_owner = &black_pieces[TI_Q];
    square_init( TI_AP, 'p', thc::a7 );
    square_init( TI_BP, 'p', thc::b7 );
    square_init( TI_CP, 'p', thc::c7 );
    square_init( TI_DP, 'p', thc::d7 );
    square_init( TI_EP, 'p', thc::e7 );
    square_init( TI_FP, 'p', thc::f7 );
    square_init( TI_GP, 'p', thc::g7 );
    square_init( TI_HP, 'p', thc::h7 );
}


bool CompressMoves::Check( bool do_internal_check, const char *description, thc::ChessPosition *external )
{
    bool ok = true;
    std::string pos1 = cr.ToDebugStr();
    std::string pos2;
    if( !do_internal_check )
    {
        printf( "Check Fail: internal tracker position not checked\n");
        ok = false;
    }
    else
    {
        thc::ChessPosition cp;
        cp.white = cr.white;
        for( int i=0; i<64; i++ )
        {
            Tracker *pt=trackers[i];
            char piece = ' ';
            if( pt && pt->in_use )
            {
                if( (int)pt->sq != i )
                {
                    printf( "Check Fail: square mismatch: %d %d\n", i, (int)pt->sq );
                    ok = false;
                }
                piece = pt->piece;
                if( pt->shadow_owner )
                {
                    Tracker *queen = pt->shadow_owner;
                    if( queen->shadow_rook && queen->shadow_rook==pt )
                        piece = ' ';
                }
            }
            cp.squares[i] = piece;
        }
        pos2 = cp.ToDebugStr();
        if( 0 != pos1.compare(pos2) )
        {
            printf( "Check Fail: internal tracker position mismatch\n");
            ok = false;
        }
    }
    if( ok )
    {
        check_last_success = pos1;
        check_last_description = description;
    }
    else
    {
        printf( "Compression/Decompression problem: %s\n", description  );
        printf( "last success position:%s", check_last_success.c_str() );
        printf( "last success description:%s\n", check_last_description.c_str() );
        if( external )
            printf( "external position:%s", external->ToDebugStr().c_str() );
        printf( "ref position:%s", pos1.c_str() );
        if( do_internal_check )
            printf( "tracker position:%s", pos2.c_str() );
    }
    //if( !ok )
    //    exit(-1);
    return ok;
}

int CompressMoves::compress_move( thc::Move mv, char *storage )
{
    int nbr_bytes=1;
    int code=0;
    int src = mv.src;
    int dst = mv.dst;
    int captured_sq = cr.squares[dst]<'A' ? -1 : dst;
    Tracker *pt = trackers[src];
    int tracker_id = pt->tracker_id;
    char piece = pt->piece;
    switch( mv.special )
    {
        case thc::NOT_SPECIAL:
        case thc::SPECIAL_BPAWN_2SQUARES:
        case thc::SPECIAL_WPAWN_2SQUARES:
        case thc::SPECIAL_KING_MOVE:
        default:
        {
            break;
        }
        case thc::SPECIAL_WK_CASTLING:
        {
            code = CODE_K_SPECIAL_WK_CASTLING;
            Tracker *rook = &white_pieces[TI_KR];
            rook->sq = thc::f1;
            trackers[thc::f1] = rook;
            trackers[thc::h1] = NULL;
            break;
        }
        case thc::SPECIAL_BK_CASTLING:
        {
            code = CODE_K_SPECIAL_BK_CASTLING;
            Tracker *rook = &black_pieces[TI_KR];
            rook->sq = thc::f8;
            trackers[thc::f8] = rook;
            trackers[thc::h8] = NULL;
            break;
        }
        case thc::SPECIAL_WQ_CASTLING:
        {
            code = CODE_K_SPECIAL_WQ_CASTLING;
            Tracker *rook = &white_pieces[TI_QR];
            rook->sq = thc::d1;
            trackers[thc::d1] = rook;
            trackers[thc::a1] = NULL;
            break;
        }
        case thc::SPECIAL_BQ_CASTLING:
        {
            code = CODE_K_SPECIAL_BQ_CASTLING;
            Tracker *rook = &black_pieces[TI_QR];
            rook->sq = thc::d8;
            trackers[thc::d8] = rook;
            trackers[thc::a8] = NULL;
            break;
        }
        case thc::SPECIAL_PROMOTION_QUEEN:
        {
            code = CODE_SPECIAL_PROMOTION_QUEEN;
            pt->piece = cr.white ? 'Q' : 'q';
            Tracker *shadow = (cr.white ? &white_pieces[15] : &black_pieces[15]);
            for( int i=0; i<16; i++,shadow-- )
            {
                if( !shadow->in_use )
                {
                    pt->shadow_rook = shadow;
                    shadow->in_use = true;
                    shadow->shadow_owner = pt;
                    shadow->piece = cr.white ? 'R' : 'r';  // shadow is a phantom rook, handles rank and file moves for new queen
                    break; // shadow successfully found, if no shadow available queen move might need two bytes
                }
            }
            break;
        }
        case thc::SPECIAL_PROMOTION_ROOK:
        {
            code = CODE_SPECIAL_PROMOTION_ROOK;
            pt->piece = cr.white ? 'R' : 'r';
            break;
        }
        case thc::SPECIAL_PROMOTION_BISHOP:
        {
            code = CODE_SPECIAL_PROMOTION_BISHOP;
            pt->piece = cr.white ? 'B' : 'b';
            break;
        }
        case thc::SPECIAL_PROMOTION_KNIGHT:
        {
            code = CODE_SPECIAL_PROMOTION_KNIGHT;
            pt->piece = cr.white ? 'N' : 'n';
            break;
        }
        case thc::SPECIAL_WEN_PASSANT:
        {
            captured_sq = SOUTH(dst);
            break;
        }
        case thc::SPECIAL_BEN_PASSANT:
        {
            captured_sq = NORTH(dst);
            break;
        }
    }
    
    switch( piece )
    {
        case 'P':
        {
            if( src-dst == 16 )
                code = 3;               // 2 square advance
            else
                code += (src-dst-7);    // 9\,8| or 7/ -> 2,1 or 0 = NW,N,NE
            // Note += because may already have CODE_SPECIAL_PROMOTION_QUEEN etc
            break;
        }
        case 'p':
        {
            if( dst-src == 16 )
                code = 3;               // 2 square advance
            else
                code += (dst-src-7);    // 9\,8| or 7/ -> 2,1 or 0 = SE,S,SW
            // Note += because may already have CODE_SPECIAL_PROMOTION_QUEEN etc
            break;
        }
        case 'K':
        case 'k':
        {
            switch( src-dst )
            {
                case   9: code = CODE_K_VECTOR_0; break; // 9\ FALL
                case   8: code = CODE_K_VECTOR_1; break; // 8|
                case   7: code = CODE_K_VECTOR_2; break; // 7/
                case   1: code = CODE_K_VECTOR_3; break; // 1-
                case  -1: code = CODE_K_VECTOR_4; break;
                case  -7: code = CODE_K_VECTOR_5; break;
                case  -8: code = CODE_K_VECTOR_6; break;
                case  -9: code = CODE_K_VECTOR_7; break;
            }
        }
        case 'N':
        case 'n':
        {
            switch( src-dst )
            {
                case  17: code = CODE_N_VECTOR_0; break;
                case  15: code = CODE_N_VECTOR_1; break;
                case  10: code = CODE_N_VECTOR_2; break;
                case   6: code = CODE_N_VECTOR_3; break;
                case -17: code = CODE_N_VECTOR_4; break;
                case -15: code = CODE_N_VECTOR_5; break;
                case -10: code = CODE_N_VECTOR_6; break;
                case  -6: code = CODE_N_VECTOR_7; break;
            }
            break;
        }
        case 'r':
        case 'R':
        {
            if( (src&7) == (dst&7) )    // same file ?
                code = CODE_SAME_FILE + ((dst>>3) & 7);   // yes, so encode new rank
            else
                code = dst & 7;                           // no, so encode new file
            break;
        }
        case 'b':
        case 'B':
        {
            int abs = (src>dst ? src-dst : dst-src);
            if( abs%9 == 0 )  // do 9 first, as LCD of 9 and 7 is 63, i.e. diff between a8 and h1, a FALL\ diagonal
            {
                code = CODE_FALL + (dst&7); // fall( = \) + dst file
            }
            else // if abs%7 == 0
            {
                code = (dst&7); // rise( = /) + dst file
            }
            break;
        }
        case 'q':
        case 'Q':
        {
            if( (src&7) == (dst&7) )                // same file ?
            {
                if( pt->shadow_rook )
                {
                    code = CODE_SAME_FILE + ((dst>>3)&7);   // yes encode rank
                    tracker_id = pt->shadow_rook->tracker_id;
                }
                else if( pt->shadow_rank )
                {
                    code = CODE_N_SHADOW + ((dst>>3)&7);    // shadow knight encodes rank
                    tracker_id = pt->shadow_rank->tracker_id;
                }
                else // no shadow available, use two bytes, first byte is a zero files diagonal move
                {
                    code = CODE_FALL + (src&7);
                    nbr_bytes = 2;
                }
            }
            else if( (src&0x38) == (dst&0x38) )     // same rank ?
            {
                if( pt->shadow_rook )
                {
                    code = (dst&7);                             // yes encode file
                    tracker_id = pt->shadow_rook->tracker_id;
                }
                else if( pt->shadow_file )
                {
                    code = CODE_N_SHADOW + (dst&7);             // shadow knight encodes file
                    tracker_id = pt->shadow_file->tracker_id;
                }
                else // no shadow available, use two bytes, first byte is a zero files diagonal move
                {
                    code = CODE_FALL + (src&7);
                    nbr_bytes = 2;
                }
            }
            else
            {
                int abs = (src>dst ? src-dst : dst-src);
                if( abs%9 == 0 )  // do 9 first, as LCD of 9 and 7 is 63, i.e. diff between a8 and h1, a FALL\ diagonal
                    code = CODE_FALL + (dst&7); // fall( = \) + dst file
                else
                    code = (dst&7); // rise( = /) + dst file
            }
            break;
        }
    }
    if( captured_sq >= 0 )
    {
        Tracker *captured = trackers[captured_sq];
        if( captured )
        {
            trackers[captured_sq] = NULL;
            if( !captured->shadow_owner )
                captured->in_use = false;
            if( captured->piece=='Q' ||captured->piece=='q')
            {
                if( captured->shadow_rook )
                    captured->shadow_rook->shadow_owner = NULL;
                if( captured->shadow_rank )
                    captured->shadow_rank->shadow_owner = NULL;
                if( captured->shadow_file )
                    captured->shadow_file->shadow_owner = NULL;
            }
        }
    }
    pt->sq = (thc::Square)dst;
    trackers[src] = NULL;
    trackers[dst] = pt;
    cr.PlayMove(mv);
    Check(true,"After compress_moves() tracker check",NULL);
    *storage = (char)(tracker_id + code);
    if( nbr_bytes > 1 )
    {
        storage++;
        *storage = 0x40 | (dst&0x3f);
    }
    return nbr_bytes;
}

int CompressMoves::decompress_move( const char *storage, thc::Move &mv )
{
    int nbr_bytes=1;
    char val = *storage;
    int lo = val & 0x0f;
    int hi = 0x0f & (val>>4);
    int tracker_id = hi;
    thc::SPECIAL special = thc::NOT_SPECIAL;
    Tracker *pt = cr.white ? &white_pieces[tracker_id] : &black_pieces[tracker_id];
    char piece = pt->piece;
    int src = (int)pt->sq;
    int dst;
    if( pt->shadow_owner )
    {
        Tracker *queen = pt->shadow_owner;
        if( queen->shadow_rook )  // captured piece that handles all rook moves for queen ?
        {
            src = queen->sq;
            if( lo & CODE_SAME_FILE )
            {
                // Same file, so change rank
                dst = ((lo<<3)&0x38) | (src&7);  // change rank
            }
            else
            {
                // Not same file, so change file
                dst = (src&0x38) | (lo&7);  // change file
            }
        }
        else if( lo & CODE_N_SHADOW )  // knight that handles rank or file moves for queen ?
        {
            src = queen->sq;
            if( tracker_id == TI_KN )   // Init() sets queen->shadow_rank to KN
            {
                // Same file, so change rank
                dst = ((lo<<3)&0x38) | (src&7);  // change rank
            }
            else                        // Init() sets queen->shadow_file to QN
            {
                // Not same file, so change file
                dst = (src&0x38) | (lo&7);  // change file
            }
        }
        else // else must be a non-shadowed knight move
        {
            switch( lo )
            {
                case CODE_N_VECTOR_0: dst = src - 17;  break;
                case CODE_N_VECTOR_1: dst = src - 15;  break;
                case CODE_N_VECTOR_2: dst = src - 10;  break;
                case CODE_N_VECTOR_3: dst = src - 6;   break;
                case CODE_N_VECTOR_4: dst = src + 17;  break;
                case CODE_N_VECTOR_5: dst = src + 15;  break;
                case CODE_N_VECTOR_6: dst = src + 10;  break;
                case CODE_N_VECTOR_7: dst = src + 6;   break;
            }
        }
    }
    else
    {
        switch( piece )
        {
            case 'N':
            case 'n':
            {
                switch( lo )
                {
                    case CODE_N_VECTOR_0: dst = src - 17;  break;
                    case CODE_N_VECTOR_1: dst = src - 15;  break;
                    case CODE_N_VECTOR_2: dst = src - 10;  break;
                    case CODE_N_VECTOR_3: dst = src - 6;   break;
                    case CODE_N_VECTOR_4: dst = src + 17;  break;
                    case CODE_N_VECTOR_5: dst = src + 15;  break;
                    case CODE_N_VECTOR_6: dst = src + 10;  break;
                    case CODE_N_VECTOR_7: dst = src + 6;   break;
                }
                break;
            }
            case 'K':
            case 'k':
            {
                special = thc::SPECIAL_KING_MOVE;
                switch( lo )
                {
                    case CODE_K_SPECIAL_WK_CASTLING:
                    {
                        special = thc::SPECIAL_WK_CASTLING;
                        Tracker *rook = &white_pieces[TI_KR];
                        rook->sq = thc::f1;
                        trackers[thc::f1] = rook;
                        trackers[thc::h1] = NULL;
                        dst = thc::g1;
                        break;
                    }
                    case CODE_K_SPECIAL_BK_CASTLING:
                    {
                        special = thc::SPECIAL_BK_CASTLING;
                        Tracker *rook = &black_pieces[TI_KR];
                        rook->sq = thc::f8;
                        trackers[thc::f8] = rook;
                        trackers[thc::h8] = NULL;
                        dst = thc::g8;
                        break;
                    }
                    case CODE_K_SPECIAL_WQ_CASTLING:
                    {
                        special = thc::SPECIAL_WQ_CASTLING;
                        Tracker *rook = &white_pieces[TI_QR];
                        rook->sq = thc::d1;
                        trackers[thc::d1] = rook;
                        trackers[thc::a1] = NULL;
                        dst = thc::c1;
                        break;
                    }
                    case CODE_K_SPECIAL_BQ_CASTLING:
                    {
                        special = thc::SPECIAL_BQ_CASTLING;
                        Tracker *rook = &black_pieces[TI_QR];
                        rook->sq = thc::d8;
                        trackers[thc::d8] = rook;
                        trackers[thc::a8] = NULL;
                        dst = thc::c8;
                        break;
                    }
                    case  CODE_K_VECTOR_0: dst = src - 9; break;
                    case  CODE_K_VECTOR_1: dst = src - 8; break;
                    case  CODE_K_VECTOR_2: dst = src - 7; break;
                    case  CODE_K_VECTOR_3: dst = src - 1; break;
                    case  CODE_K_VECTOR_4: dst = src + 1; break;
                    case  CODE_K_VECTOR_5: dst = src + 7; break;
                    case  CODE_K_VECTOR_6: dst = src + 8; break;
                    case  CODE_K_VECTOR_7: dst = src + 9; break;
                }
                break;
            }
            case 'Q':
            case 'q':
            case 'B':
            case 'b':
            {
                if( lo & CODE_FALL )
                {
                    // FALL\ + file
                    int file_delta = (lo&7) - (src&7);
                    if( file_delta == 0 ) //exception code signals 2 byte mode
                    {
                        storage++;
                        dst = *storage & 0x3f;
                        nbr_bytes = 2;
                    }
                    else
                        dst = src + 9*file_delta;   // eg src=a8(0), dst=h1(63), file_delta=7  -> 9*7 =63
                }
                else
                {
                    // RISE/ + file
                    int file_delta = (lo&7) - (src&7);
                    dst = src - 7*file_delta;   // eg src=h8(7), dst=a1(56), file_delta=7  -> 7*7 =49
                }
                break;
            }
            case 'R':
            case 'r':
            {
                if( lo & CODE_SAME_FILE )
                {
                    dst = ((lo<<3)&0x38) | (src&7);  // change rank
                }
                else
                {
                    dst = (src&0x38) | (lo&7);  // change file
                }
                break;
            }
            case 'P':
            {
                if( (src&0x38) == 8 ) // if a7(8) - h7(15)
                {
                    switch( lo & 0x0c )
                    {
                        case CODE_SPECIAL_PROMOTION_QUEEN:
                        {
                            special = thc::SPECIAL_PROMOTION_QUEEN;
                            pt->piece = 'Q';
                            Tracker *shadow = &white_pieces[15];
                            for( int i=0; i<16; i++,shadow-- )
                            {
                                if( !shadow->in_use )
                                {
                                    pt->shadow_rook = shadow;
                                    shadow->in_use = true;
                                    shadow->shadow_owner = pt;
                                    shadow->piece = 'R';  // shadow is a phantom rook, handles rank and file moves for new queen
                                    break;
                                }
                            }
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_ROOK:
                        {
                            special = thc::SPECIAL_PROMOTION_ROOK;
                            pt->piece = 'R';
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_BISHOP:
                        {
                            special = thc::SPECIAL_PROMOTION_BISHOP;
                            pt->piece = 'B';
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_KNIGHT:
                        {
                            special = thc::SPECIAL_PROMOTION_KNIGHT;
                            pt->piece = 'N';
                            break;
                        }
                    }
                    
                }
                if( (lo&3) == 3 )
                {
                    special = thc::SPECIAL_WPAWN_2SQUARES;
                    dst = src-16;
                }
                else
                {
                    dst = src-(lo&3)-7; // 2\,1| or 0/ -> -9,-8 or -7
                    if( !(lo&1) )    // if( 2\ or 0/ )
                    {
                        if( cr.squares[dst]==' ' && cr.squares[SOUTH(dst)]=='p' )
                            special = thc::SPECIAL_WEN_PASSANT;
                    }
                }
                break;
            }
                
            case 'p':
            {
                if( (src&0x38) == 0x30 ) // if a2(48) - h2(55)
                {
                    switch( lo & 0x0c )
                    {
                        case CODE_SPECIAL_PROMOTION_QUEEN:
                        {
                            special = thc::SPECIAL_PROMOTION_QUEEN;
                            pt->piece = 'q';
                            Tracker *shadow = &black_pieces[15];
                            for( int i=0; i<16; i++,shadow-- )
                            {
                                if( !shadow->in_use )
                                {
                                    pt->shadow_rook = shadow;
                                    shadow->in_use = true;
                                    shadow->shadow_owner = pt;
                                    shadow->piece = 'r';  // shadow is a phantom rook, handles rank and file moves for new queen
                                    break;
                                }
                            }
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_ROOK:
                        {
                            special = thc::SPECIAL_PROMOTION_ROOK;
                            pt->piece = 'r';
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_BISHOP:
                        {
                            special = thc::SPECIAL_PROMOTION_BISHOP;
                            pt->piece = 'b';
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_KNIGHT:
                        {
                            special = thc::SPECIAL_PROMOTION_KNIGHT;
                            pt->piece = 'n';
                            break;
                        }
                    }
                    
                }
                if( (lo&3) == 3 )
                {
                    special = thc::SPECIAL_BPAWN_2SQUARES;
                    dst = src+16;
                }
                else
                {
                    dst = src+(lo&3)+7; // 2\,1| or 0/ -> +9,+8 or +7
                    if( !(lo&1) )    // if( 2\ or 0/ )
                    {
                        if( cr.squares[dst]==' ' && cr.squares[NORTH(dst)]=='P' )
                            special = thc::SPECIAL_BEN_PASSANT;
                    }
                }
                break;
            }
        }
    }
    
    mv.src = (thc::Square)src;
    mv.dst = (thc::Square)dst;
    mv.special = special;
    mv.capture = ' ';
    int captured_sq = cr.squares[dst]<'A' ? -1 : dst;
    if( special == thc::SPECIAL_BEN_PASSANT )
        captured_sq = NORTH(dst);
    else if( special == thc::SPECIAL_WEN_PASSANT )
        captured_sq = SOUTH(dst);
    pt = trackers[src];
    char desc[100];
    sprintf( desc, "pt is NULL; decompress_move() code=%02x, src=%s, dst=%s", val&0xff, SqToStr(src).c_str(), SqToStr(dst).c_str() );
    if( pt == NULL )
    {
        Check( false, desc, NULL );
    }
    if( captured_sq >= 0 )
    {
        mv.capture = cr.squares[captured_sq];
        Tracker *captured = trackers[captured_sq];
        if( captured )
        {
            trackers[captured_sq] = NULL;
            if( !captured->shadow_owner )
                captured->in_use = false;
            if( captured->piece=='Q' ||captured->piece=='q')
            {
                if( captured->shadow_rook )
                    captured->shadow_rook->shadow_owner = NULL;
                if( captured->shadow_rank )
                    captured->shadow_rank->shadow_owner = NULL;
                if( captured->shadow_file )
                    captured->shadow_file->shadow_owner = NULL;
            }
        }
    }
    pt->sq = (thc::Square)dst;
    trackers[src] = NULL;
    trackers[dst] = pt;
    cr.PlayMove(mv);
    Check( true, desc+12, NULL );
    return nbr_bytes;
}

void CompressMoves::decompress_move_stay( const char *storage, thc::Move &mv ) const // decompress but don't advance
{
    int nbr_bytes=1;
    char val = *storage;
    int lo = val & 0x0f;
    int hi = 0x0f & (val>>4);
    int tracker_id = hi;
    thc::SPECIAL special = thc::NOT_SPECIAL;
    const Tracker *pt = cr.white ? &white_pieces[tracker_id] : &black_pieces[tracker_id];
    char piece = pt->piece;
    int src = (int)pt->sq;
    int dst;
    if( pt->shadow_owner )
    {
        Tracker *queen = pt->shadow_owner;
        if( queen->shadow_rook )  // captured piece that handles all rook moves for queen ?
        {
            src = queen->sq;
            if( lo & CODE_SAME_FILE )
            {
                // Same file, so change rank
                dst = ((lo<<3)&0x38) | (src&7);  // change rank
            }
            else
            {
                // Not same file, so change file
                dst = (src&0x38) | (lo&7);  // change file
            }
        }
        else if( lo & CODE_N_SHADOW )  // knight that handles rank or file moves for queen ?
        {
            src = queen->sq;
            if( tracker_id == TI_KN )   // Init() sets queen->shadow_rank to KN
            {
                // Same file, so change rank
                dst = ((lo<<3)&0x38) | (src&7);  // change rank
            }
            else                        // Init() sets queen->shadow_file to QN
            {
                // Not same file, so change file
                dst = (src&0x38) | (lo&7);  // change file
            }
        }
        else // else must be a non-shadowed knight move
        {
            switch( lo )
            {
                case CODE_N_VECTOR_0: dst = src - 17;  break;
                case CODE_N_VECTOR_1: dst = src - 15;  break;
                case CODE_N_VECTOR_2: dst = src - 10;  break;
                case CODE_N_VECTOR_3: dst = src - 6;   break;
                case CODE_N_VECTOR_4: dst = src + 17;  break;
                case CODE_N_VECTOR_5: dst = src + 15;  break;
                case CODE_N_VECTOR_6: dst = src + 10;  break;
                case CODE_N_VECTOR_7: dst = src + 6;   break;
            }
        }
    }
    else
    {
        switch( piece )
        {
            case 'N':
            case 'n':
            {
                switch( lo )
                {
                    case CODE_N_VECTOR_0: dst = src - 17;  break;
                    case CODE_N_VECTOR_1: dst = src - 15;  break;
                    case CODE_N_VECTOR_2: dst = src - 10;  break;
                    case CODE_N_VECTOR_3: dst = src - 6;   break;
                    case CODE_N_VECTOR_4: dst = src + 17;  break;
                    case CODE_N_VECTOR_5: dst = src + 15;  break;
                    case CODE_N_VECTOR_6: dst = src + 10;  break;
                    case CODE_N_VECTOR_7: dst = src + 6;   break;
                }
                break;
            }
            case 'K':
            case 'k':
            {
                special = thc::SPECIAL_KING_MOVE;
                switch( lo )
                {
                    case CODE_K_SPECIAL_WK_CASTLING:
                    {
                        special = thc::SPECIAL_WK_CASTLING;
                        dst = thc::g1;
                        break;
                    }
                    case CODE_K_SPECIAL_BK_CASTLING:
                    {
                        special = thc::SPECIAL_BK_CASTLING;
                        dst = thc::g8;
                        break;
                    }
                    case CODE_K_SPECIAL_WQ_CASTLING:
                    {
                        special = thc::SPECIAL_WQ_CASTLING;
                        dst = thc::c1;
                        break;
                    }
                    case CODE_K_SPECIAL_BQ_CASTLING:
                    {
                        special = thc::SPECIAL_BQ_CASTLING;
                        dst = thc::c8;
                        break;
                    }
                    case  CODE_K_VECTOR_0: dst = src - 9; break;
                    case  CODE_K_VECTOR_1: dst = src - 8; break;
                    case  CODE_K_VECTOR_2: dst = src - 7; break;
                    case  CODE_K_VECTOR_3: dst = src - 1; break;
                    case  CODE_K_VECTOR_4: dst = src + 1; break;
                    case  CODE_K_VECTOR_5: dst = src + 7; break;
                    case  CODE_K_VECTOR_6: dst = src + 8; break;
                    case  CODE_K_VECTOR_7: dst = src + 9; break;
                }
                break;
            }
            case 'Q':
            case 'q':
            case 'B':
            case 'b':
            {
                if( lo & CODE_FALL )
                {
                    // FALL\ + file
                    int file_delta = (lo&7) - (src&7);
                    if( file_delta == 0 ) //exception code signals 2 byte mode
                    {
                        storage++;
                        dst = *storage & 0x3f;
                        nbr_bytes = 2;
                    }
                    else
                        dst = src + 9*file_delta;   // eg src=a8(0), dst=h1(63), file_delta=7  -> 9*7 =63
                }
                else
                {
                    // RISE/ + file
                    int file_delta = (lo&7) - (src&7);
                    dst = src - 7*file_delta;   // eg src=h8(7), dst=a1(56), file_delta=7  -> 7*7 =49
                }
                break;
            }
            case 'R':
            case 'r':
            {
                if( lo & CODE_SAME_FILE )
                {
                    dst = ((lo<<3)&0x38) | (src&7);  // change rank
                }
                else
                {
                    dst = (src&0x38) | (lo&7);  // change file
                }
                break;
            }
            case 'P':
            {
                if( (src&0x38) == 8 ) // if a7(8) - h7(15)
                {
                    switch( lo & 0x0c )
                    {
                        case CODE_SPECIAL_PROMOTION_QUEEN:
                        {
                            special = thc::SPECIAL_PROMOTION_QUEEN;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_ROOK:
                        {
                            special = thc::SPECIAL_PROMOTION_ROOK;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_BISHOP:
                        {
                            special = thc::SPECIAL_PROMOTION_BISHOP;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_KNIGHT:
                        {
                            special = thc::SPECIAL_PROMOTION_KNIGHT;
                            break;
                        }
                    }
                    
                }
                if( (lo&3) == 3 )
                {
                    special = thc::SPECIAL_WPAWN_2SQUARES;
                    dst = src-16;
                }
                else
                {
                    dst = src-(lo&3)-7; // 2\,1| or 0/ -> -9,-8 or -7
                    if( !(lo&1) )    // if( 2\ or 0/ )
                    {
                        if( cr.squares[dst]==' ' && cr.squares[SOUTH(dst)]=='p' )
                            special = thc::SPECIAL_WEN_PASSANT;
                    }
                }
                break;
            }
                
            case 'p':
            {
                if( (src&0x38) == 0x30 ) // if a2(48) - h2(55)
                {
                    switch( lo & 0x0c )
                    {
                        case CODE_SPECIAL_PROMOTION_QUEEN:
                        {
                            special = thc::SPECIAL_PROMOTION_QUEEN;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_ROOK:
                        {
                            special = thc::SPECIAL_PROMOTION_ROOK;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_BISHOP:
                        {
                            special = thc::SPECIAL_PROMOTION_BISHOP;
                            break;
                        }
                        case CODE_SPECIAL_PROMOTION_KNIGHT:
                        {
                            special = thc::SPECIAL_PROMOTION_KNIGHT;
                            break;
                        }
                    }
                    
                }
                if( (lo&3) == 3 )
                {
                    special = thc::SPECIAL_BPAWN_2SQUARES;
                    dst = src+16;
                }
                else
                {
                    dst = src+(lo&3)+7; // 2\,1| or 0/ -> +9,+8 or +7
                    if( !(lo&1) )    // if( 2\ or 0/ )
                    {
                        if( cr.squares[dst]==' ' && cr.squares[NORTH(dst)]=='P' )
                            special = thc::SPECIAL_BEN_PASSANT;
                    }
                }
                break;
            }
        }
    }
    
    mv.src = (thc::Square)src;
    mv.dst = (thc::Square)dst;
    mv.special = special;
    mv.capture = ' ';
    int captured_sq = cr.squares[dst]<'A' ? -1 : dst;
    if( special == thc::SPECIAL_BEN_PASSANT )
        captured_sq = NORTH(dst);
    else if( special == thc::SPECIAL_WEN_PASSANT )
        captured_sq = SOUTH(dst);
    if( captured_sq >= 0 )
    {
        mv.capture = cr.squares[captured_sq];
    }
}

