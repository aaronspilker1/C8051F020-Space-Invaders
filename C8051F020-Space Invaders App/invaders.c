#include <C8051F020.h>
#include <string.h>
#include <stdlib.h>
#include <lcd.h>
#include <sound.h>
#include <graphics.h>


xdata char score_str[] = "Score: 0000";
xdata char lives_str[] = "  Lives: 0";
xdata char wave_num_str[] = "Wave #00";
code char game_over_str[] = "GAME OVER!";
code char intro_str[] = "SPACE INVADERS        (Press Start)";
xdata unsigned char ship_frame_num = 0; // ship animation frame number
xdata unsigned char ship_alive[8][2] = {0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0}; // ship status, column/row
xdata int ship_ref_coords[2] = {0, 8}; // ship screen reference coords, x/y, top-left of ship group
xdata int ship_x_coord_limits[2] = {0, 27}; // ship reference x-coord limits, min/max
xdata char ship_shot_coords[8][2] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1};
xdata char cannon_shot_coords[4][2] = {-1, -1, -1, -1,
                                       -1, -1, -1, -1};
unsigned int score = 0;
unsigned char lives = 3;
unsigned char ship_update_counter = 0;
unsigned char ship_update_time = 0;
unsigned char ship_direction = 0;
unsigned char wave_num = 0;
unsigned char game_is_over = 0;
unsigned char cannon_size = 7;
unsigned int cannon_position_avg = 0;
xdata unsigned int cannon_position[32] = {0};
unsigned char cannon_position_index = 0;
unsigned char sound_to_play = 0;
unsigned char sound_in_progress = 0;
unsigned int sound_index = 0;
unsigned char first_run = 1;
char button_last = 0;
char button_current = 0;
char switch_current = 0;
sbit adc0int = ADC0CN^5;


void draw_strings(void)
{
    int cursor = 0;
    int i = 0;
    int j = 0;
    char cur_char;
    
    // setup score string with updated values and add to screen data
    score_str[7] = ((score % 10000) / 1000) + 0x30; // 1000's digit to char
    score_str[8] = ((score % 1000) / 100) + 0x30; // 100's digit to char
    score_str[9] = ((score % 100) / 10) + 0x30; // 10's digit to char
    score_str[10] = (score % 10) + 0x30; // 1's digit to char
    for (i = 0; i < strlen(score_str); i++) // display score string
    {
        cur_char = score_str[i];
        if (cur_char < 0x20)
            continue; // skip invalid characters
            cur_char -= 0x20; // align ascii value with our font array
        for (j = 0; j < 5; j++) // handle each column in character
        {
            screen[cursor] = font5x8[cur_char*5+j];
            cursor++;
        }
        screen[cursor] = 0x00; // kerning gap
        cursor++;
    }

    // setup lives string with updated values and add to screen data
    lives_str[9] = (lives % 10) + 0x30; // 1's digit to char
    for (i = 0; i < strlen(lives_str); i++) // display lives string
    {
        cur_char = lives_str[i];
        if (cur_char < 0x20)
            continue; // skip invalid characters
            cur_char -= 0x20; // align ascii value with our font array
        for (j = 0; j < 5; j++) // handle each column in character
        {
            screen[cursor] = font5x8[cur_char*5+j];
            cursor++;
        }
        screen[cursor] = 0x00; // kerning gap
        cursor++;
    }
}


void draw_cannon(void)
{
    unsigned char i = 0;
    unsigned char cannon_num = switch_current & 0x03; //last two switches

    switch (cannon_num)
    {
        case 3:
            cannon_size = 13;
            break;
        case 2:
            cannon_size = 11;
            break;
        case 1:
            cannon_size = 9;
            break;
        case 0:
        default:
            cannon_size = 7;
            break;
    }
    // draw cannon
    for (i = 0; i < cannon_size; i++)
        screen[896+cannon_position_avg+i] = cannon_gfx[cannon_num][i]; // 896 = page 7
}


void calculate_ships_position(void)
{
    unsigned char i = 0;
    unsigned char lower_limit = 48;
    unsigned char ship_in_bottom_row = 0;

    // reset ship x coord limits before calculations below
    ship_x_coord_limits[0] = 0;
    ship_x_coord_limits[1] = 27;

    // set right limit based on how many right columns are gone
    for (i = 7; i >= 0; i--)
    {
        if (!(ship_alive[i][0]) && !(ship_alive[i][1]))
        {
            ship_x_coord_limits[1] += 13; // make sure we move all the way to the right edge
        }
        else
        {
            break; // we found a rightmost ship so stop checking
        }
    }

    // set left limit based on how many left columns are gone
    for (i = 0; i < 8; i++)
    {
        if (!(ship_alive[i][0]) && !(ship_alive[i][1]))
        {
            ship_x_coord_limits[0] -= 13; // make sure we move all the way to the left edge
        }
        else
        {
            break; // we found a leftmost ship so stop checking
        }
    }

    // move ships in the current direction one pixel
    // also check if we're at the edge, if so, drop down and reverse direction
    if (ship_direction == 0) // moving right
    {
        ship_ref_coords[0] += 1;
        if (ship_ref_coords[0] >= ship_x_coord_limits[1]) // check right limit
        {
            ship_ref_coords[0] = ship_x_coord_limits[1]; // reset x-coord to limit
            ship_ref_coords[1] += 8; // drop down one row
            ship_direction = 1; // reverse direction
        }
    }
    else // moving left
    {
        ship_ref_coords[0] -= 1;
        if (ship_ref_coords[0] <= ship_x_coord_limits[0]) // check left limit
        {
            ship_ref_coords[0] = ship_x_coord_limits[0]; // reset x-coord to limit
            ship_ref_coords[1] += 8; // drop down one row
            ship_direction = 0; // reverse direction
        }
    }

    // see if we have any ships in the bottom row for lower limit calculation
    for (i = 0; i < 8; i++)
        if (ship_alive[i][1])
            ship_in_bottom_row = 1;
    // make sure we don't drop below bottom of screen
    if (ship_in_bottom_row)
    {
        if (ship_ref_coords[1] > 48) // ship(s) in bottom row
            ship_ref_coords[1] = 48;
    }
    else // no ships in bottom row
    {
        if (ship_ref_coords[1] > 56)
            ship_ref_coords[1] = 56;
    }

    // switch ship animation frames each time we move
    ship_frame_num++;
    ship_frame_num &= 0x01; // we use this as an index so restrict values

}


void draw_ships(void)
{
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char k = 0;
    int x = 0;
    int y = 0;
    unsigned int cursor = 0;

    x = ship_ref_coords[0];
    y = ship_ref_coords[1];
    for (i = 0; i < 2; i++)
    {
        // page*128 + x puts us at reference coords in screen[], i gives ship row num
        cursor = (((y/8)+i)*128) + x;
        for (j = 0; j < 8; j++)
        {
            // don't try to draw outside ship boundaries or draw nonexistent ships
            if ((cursor < 128) || (cursor > 895) || !(ship_alive[j][i]))
            {
                cursor += 13; // 13 = width of ship + spacing
                continue;
            }
            // draw the current frame of the ship
            for (k = 0; k < 10; k++) // 10 = width of ship
            {
                screen[cursor] = ship_gfx[ship_frame_num][k];
                cursor++;
            }
            // add in spacing between ships
            for (k = 0; k < 3; k++) // 3 = ship spacing
            {
                screen[cursor] = 0x00;
                cursor++;
            }
        }
    }
}


void calculate_shots_position(void)
{
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char k = 0;
    unsigned char rand_num = 0;
    unsigned char ship_num = 0;
    unsigned char num_ships_remaining = 0;

    // move the cannon shots one pixel
    for (i = 0; i < 4; i++)
    {
        if (cannon_shot_coords[i][0] != -1) // only update active shots
        {
            cannon_shot_coords[i][1] -= 1;  // move up one pixel
            if (cannon_shot_coords[i][1] < 8)
            {
                cannon_shot_coords[i][0] = -1; // clear shot if we went past top boundary
                cannon_shot_coords[i][1] = -1;
            }
        }
    }

    // move the ship shots one pixel
    for (i = 0; i < 8; i++)
    {
        if (ship_shot_coords[i][0] != -1) // only update active shots
        {
            ship_shot_coords[i][1] += 1;  // move down one pixel
            if (ship_shot_coords[i][1] > 63) // see if shot went offscreen
            {
                ship_shot_coords[i][0] = -1; // clear shot if we went past top boundary
                ship_shot_coords[i][1] = -1;
            }
        }
    }


    

    // decide when and where to place random ship shot
    rand_num = rand() % ship_update_time; // generate a random number based on current speed (difficulty)
    if (rand_num == 0)
    {
        rand_num = (rand() % 16); // pick a ship to fire from
        k = 0;
        i = (rand_num % 8); // 0-7 (column)
        j = (rand_num / 8); // 0-1 (row)
        if(ship_alive[i][j])
        {
            // place shot coords in first free slot
            for (k = 0; k < 8; k++)
            {
                if (ship_shot_coords[k][0] == -1) // ignore unused shot slots
                {
                    // set x/y coords of ship shot
                    ship_shot_coords[k][0] = i*13 + ship_ref_coords[0] + 5; // center shot on ship (ship width /2 = 5)
                    ship_shot_coords[k][1] = j*8 + ship_ref_coords[1] + 6; // top/bottom row
                    break; // stored shot, so stop checking for free slots
                }
            }
        }
    }
}


void draw_shots(void)
{
    char x = 0;
    char y = 0;
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char cur_char = 0;
    unsigned char tail_length = ((switch_current & 0x0C) >> 2) + 4; // switches 2 & 3, tail_length of 4-7

    // draw shots
    // alien ship shots
    for (i = 0; i < 8; i++)
    {
        if (ship_shot_coords[i][0] == -1) // if -1 then unused shot slot
            continue;
        
        for (j = 0; j < tail_length; j++) // draw shot and tail after it
        {
            x = ship_shot_coords[i][0];
            y = ship_shot_coords[i][1] - j;
            // calculate char value from coords
            // page = y_coord / 8
            // shift_bits = y_coord % 8, bit in char
            // char = 1 << shift_bits
            cur_char = 1 << (y % 8);
            screen[((y/8)*128)+x] |= cur_char;
        }
    }
    // cannon shots
    for (i = 0; i < 4; i++)
    {
        if (cannon_shot_coords[i][0] == -1) // if -1 then unused shot slot
            continue;

        for (j = 0; j < tail_length; j++) // draw shot and tail after it
        {
            x = cannon_shot_coords[i][0];
            y = cannon_shot_coords[i][1] + j;
            // calculate char value from coords
            // page = y_coord / 8
            // shift_bits = y_coord % 8, bit in char
            // char = 1 << shift_bits
            cur_char = 1 << (y % 8);
            screen[((y/8)*128)+x] |= cur_char;
        }
    }
}


void initialize_wave(void)
{
    unsigned char i = 0;
    unsigned char j = 0;

    // set up new wave of ships
    ship_frame_num = 0; // reset animation frame
    ship_direction = 0; // reet wave movement direction
    ship_x_coord_limits[0] = 0; // reset ship x-coord limits (for movement)
    ship_x_coord_limits[1] = 27;
    for (i = 0; i < 8; i++)
        for (j = 0; j < 2; j++)
            ship_alive[i][j] = 1; // reset ship alive/dead state
    wave_num++;
    for (i = 0; i < 8; i++)
        for (j = 0; j < 2; j++)
            ship_shot_coords[i][j] = -1; // clear remaining ship shots from last wave
    for (i = 0; i < 4; i++)
        for (j = 0; j < 2; j++)
            cannon_shot_coords[i][j] = -1; // clear remaining cannon shots from last wave
    ship_ref_coords[0] = 0; // reset ship wave starting point
    ship_ref_coords[1] = wave_num*8; // new wave is 8 pixels lower than last (first wave at 8)
    if (ship_ref_coords[1] >= 24)
        ship_ref_coords[1] = 24; // but no more than 16 pixels lower than first wave
    if (wave_num < 6) // make sure we don't have a negative time
    {
        ship_update_time = 60 - (wave_num*10); // 50ms starting update delay first wave, 40ms second wave, etc
    }
    else // just set our lowest wave start update delay
    {
        ship_update_time = 10; // bottom wave start update delay limit is 10ms
    }
}


void check_start_game(void)
{
    if (button_current & 0x40)
    {
        // clear button press
        button_current &= 0xBF;

        // start game if not already started
        if (game_is_over)
        {
            game_is_over = 0;
            initialize_wave();
        }
    }
}


void check_cannon_fired(void)
{
    unsigned char i = 0;

    // see if we fired the cannon (and game is in progress)
    if ((button_current & 0x80) && (game_is_over == 0))
    {
        // clear button press
        button_current &= 0x7F;
        // place shot coords in first free slot
        for (i = 0; i < 4; i++)
        {
            if (cannon_shot_coords[i][0] == -1) // ignore unused shot slots
            {
                // play cannon fire sound
                sound_to_play = 2;
                // set x/y coords of cannon shot
                cannon_shot_coords[i][0] = cannon_position_avg + cannon_size/2; // center shot on cannon
                cannon_shot_coords[i][1] = 55; // bottom of page 6
                break; // stored shot, so stop checking for free slots
            }
        }
    }
}


void calculate_hit(void)
{
    char x = 0;
    char y = 0;
    unsigned char i = 0;
    unsigned char j = 0;

    // check if cannon shot(s) hit ship(s)
    for (i = 0; i < 4; i++)
    {
        if (cannon_shot_coords[i][0] == -1) // if -1 then unused shot slot
            continue;
        x = cannon_shot_coords[i][0];
        y = cannon_shot_coords[i][1];
        // check if y-coord of shot is in top row of ships
        if ((y >= ship_ref_coords[1]) && (y <= ship_ref_coords[1] + 7))
        {
            // check x-coord against alive ships
            for (j = 0; j < 8; j++)
            {
                if ((ship_alive[j][0]) && (x >= (ship_ref_coords[0] + j*13)) && (x <= (ship_ref_coords[0] + j*13 + 9)))
                {
                    // we hit a ship, remove it and the shot that hit it
                    ship_alive[j][0] = 0;
                    cannon_shot_coords[i][0] = -1;
                    cannon_shot_coords[i][1] = -1;
                    score++;
                    if (ship_update_time > 1) // see if we can speed up
                        ship_update_time--; // if so, speed up remaining ships 
                    // play ship hit sound
                    sound_to_play = 1;
                }
            }
        }
        // check if y-coord of shot is in bottom row of ships
        else if ((y >= ship_ref_coords[1] + 8) && (y <= ship_ref_coords[1] + 15))
        {
            // check x-coord against alive ships
            for (j = 0; j < 8; j++)
            {
                if ((ship_alive[j][1]) && (x >= (ship_ref_coords[0] + j*13)) && (x <= (ship_ref_coords[0] + j*13 + 9)))
                {
                    // we hit a ship, remove it and the shot that hit it
                    ship_alive[j][1] = 0;
                    cannon_shot_coords[i][0] = -1;
                    cannon_shot_coords[i][1] = -1;
                    score++;
                    if (ship_update_time > 1) // see if we can speed up
                        ship_update_time--; // if so, speed up remaining ships
                    // play ship hit sound
                    sound_to_play = 1;
                }
            }
        }
    }

    // check if ship shot(s) hit cannon
    for (i = 0; i < 8; i++)
    {
        if (ship_shot_coords[i][0] == -1) // if -1 then unused shot slot
            continue;
        x = ship_shot_coords[i][0];
        y = ship_shot_coords[i][1];
        // check if coords of shot is within current cannon area (page 7 and cannon position/size)
        if ((y >= 56) && (x >= cannon_position_avg) && (x <= (cannon_position_avg + cannon_size)))
        {
            // we hit the cannon
            // play cannon hit sound
            sound_to_play = 3;
            // reset wave
            lives--;
            wave_num--;
            initialize_wave();
        }
    }
}


void game_over(void)
{
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char cur_char = 0;
    int cursor = 0;

    // stop updates
    game_is_over = 1;

    // print game over text
    blank_screen();
    cursor = 416;
    for (i = 0; i < strlen(game_over_str); i++)
    {
        cur_char = game_over_str[i];
        if (cur_char < 0x20)
            continue; // skip invalid characters
        cur_char -= 0x20; // align ascii value with our font array
        for (j = 0; j < 5; j++) // handle each column in character
        {
            screen[cursor] = font5x8[cur_char*5+j];
            cursor++;
        }
        screen[cursor] = 0x00; // kerning gap
        cursor++;
    }

    // set up for new game
    wave_num = 0;
    lives = 3;
    score = 0;
}


void check_status(void)
{
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char num_ships_remaining = 0;
    unsigned char ship_in_bottom_row = 0;

    // check alive ships
    for (i = 0; i < 8; i++)
        for (j = 0; j < 2; j++)
            if(ship_alive[i][j])
                num_ships_remaining++;
    // and set up next wave if cleared
    if (num_ships_remaining == 0)
    {
        initialize_wave();
    }

    // see if we have any ships in the bottom row
    for (i = 0; i < 8; i++)
        if (ship_alive[i][1])
            ship_in_bottom_row = 1;
    // check if ships ran into cannon
    if ((ship_in_bottom_row && (ship_ref_coords[1] > 47)) || (ship_ref_coords[1] > 55))
    {
        // play cannon hit sound
        sound_to_play = 3;
        // and remove life and reset wave
        lives--;
        wave_num--;
        initialize_wave();
    }

    // check if lives=0 and do game over tasks if so
    if (lives == 0)
    {
        game_over();
    }
}


void set_dac_value(void)
{
    unsigned int sound_data = 0;
    
    // sounds are only 2048 long, if we went that far we finished playing it
    if (sound_index > 2047)
    {
        sound_index = 0; // reset sound data index
        sound_in_progress = 0; // clear sound in progress
        // make sure DAC output is 0 (silence) if we're done with playback
        DAC0L = 0;
        DAC0H = 0; // write is latched on DAC0H write so write last
    }
    if (sound_in_progress)
    {
        switch (sound_in_progress) // determine which sound we're playing
        {
            case 1:
                sound_data = ship_death_snd[sound_index];
                break;
            case 2:
                sound_data = cannon_fire_snd[sound_index];
                break;
            case 3:
                sound_data = cannon_death_snd[sound_index];
                break;
            default: // unknown sound requested
                sound_data = 0x000;
                break;
        }
        // put the sound data into the DAC registers
        DAC0L = sound_data;
        DAC0H = sound_data >> 8; // write is latched on DAC0H write so write last
        sound_index++; // next loop will play next part of sound
    }
    else if (sound_to_play) // no sound in progress, see if there's one queued
    {
        sound_in_progress = sound_to_play; // start playback of queued sound
        sound_to_play = 0; // clear queued sound
    }      
}


void get_adc0_value(void)
{
    unsigned int position_temp = 0;
    unsigned int i = 0;
    unsigned long position_sum = 0;

    // get ADC0 value and insert into array
    while (!adc0int); // wait for ADC0 to finish conversion
    adc0int = 0;
    position_temp = ADC0H;
    position_temp = (position_temp << 8);
    position_temp = position_temp | ADC0L;

    // read from ADC and store in the correct spot
    cannon_position[cannon_position_index] = position_temp;
    cannon_position_index++;
    if (cannon_position_index > 31)
        cannon_position_index = 0;

    // if we have enough values then calculate the current average
    if (cannon_position_index == 0)
    {
        position_sum = 0;
        for (i = 0; i < 32; i++)
            position_sum += cannon_position[i]; // sum the adc samples
        cannon_position_avg = position_sum/32; // calculate the average
        cannon_position_avg = cannon_position_avg/32; // scale to 0-127
        if (cannon_position_avg > (127 - cannon_size)) // make sure we can fit on screen
            cannon_position_avg = 127 - cannon_size;
    }
}


void check_buttons(void)
{
    unsigned char button_state = 0;

    // read buttons
    button_state = ~P5; // set 1's where button pressed
    button_state &= 0xC0; // isolate the button bits
    button_current = button_state ^ button_last; // only get changed states
    button_current &= button_state; // only get presses
    button_last = button_state; // save current state for next check 

    // get DIP switch settings
    switch_current = ~P5;
    switch_current &= 0x0F; // isolate switch bits
}


void timer0(void) interrupt 1 // 10ms
{
    TF0 = 0; // reset timer 0 overflow flag

    check_buttons();
    check_start_game();

    if (game_is_over == 0) // only clear and update screen if we started game
    {
        // clear old data
        blank_screen();

        // draw the various parts of the screen
        draw_strings(); // page 0
        draw_ships(); // pages 1-6
        draw_cannon(); // page 7
        draw_shots(); // pages 1-7
    }
    
    // display our new data
    refresh_screen();
}


void timer1(void) interrupt 3 // 1ms
{
    TF1 = 0; // reset timer 1 overflow flag

    if (game_is_over == 0) // don't run tasks if in "game over" state
    {
        check_cannon_fired();
        ship_update_counter++;
        if (ship_update_counter > ship_update_time)
        {
            ship_update_counter = 0;
            calculate_ships_position();
        }
        calculate_shots_position();
        calculate_hit();
        check_status();
    }
}


void timer2(void) interrupt 5 // 100us
{
    TF2 = 0; // reset timer 2 overflow flag

    get_adc0_value();
    set_dac_value();
}


void main(void) 
{
    unsigned char i = 0;
    unsigned char j = 0;
    unsigned char cur_char = 0;
    int cursor = 0;

    WDTCN = 0xDE;   // disable watchdog
    WDTCN = 0xAD;
    XBR2 = 0x40;    // enable port output
    OSCXCN = 0x67;  // turn on external crystal
    TMOD = 0x22;    // wait 1ms using T1 mode 2
    TH1 = -167;     // 22MHz clock, 167 counts - 1ms
    TR1 = 1;
    while ( TF1 == 0 ) { }          // wait 1ms
    while ( !(OSCXCN & 0x80) ) { }  // wait till oscillator stable
    OSCICN = 8;     // switch over to 22.1184MHz
    TMOD = 0x11;    // configure for 16-bit timer operation
    TH0 = -18432 >> 8;     // 22MHz clock, 18432 counts - 10ms
    TL0 = -18432;
    TR0 = 1;        // start timer 0
    TH1 = -1843 >> 8;     // 22MHz clock, 1843 counts - 1ms
    TL1 = -1843;
    TR1 = 1;        // start timer 1
    T2CON = 0x00; // timer 2 16-bit auto-reload mode
    RCAP2H = -184 >> 8; // 22MHz clock, /12, 184 counts - 100us
    RCAP2L = -184;
    REF0CN = 0x03;  // set ADC0 reference to 2.4V
    AMX0CF = 0x00;  // all inputs are single-ended for ADC0
    AMX0SL = 0x00;  // change ADC0 register to external analog (potentiometer)
    ADC0CF = 0x40;  // change gain to 1 (last three bits)
    ADC0CN = 0x8C;  // enable ADC0, enable tracking mode and set trigger to timer 2 overflow
	DAC0CN = 0x80; 	// DAC update on DAC0H write, right justified
    TR2 = 1; // start timer 2


    // enable interrupts
    IE = 0xAA;

    // set up and clear lcd
    init_lcd();
    blank_screen();

    // put game into "new game" state
    game_is_over = 1;
    wave_num = 0;
    lives = 3;
    score = 0;

    // print intro text
    cursor = 410;
    for (i = 0; i < strlen(intro_str); i++)
    {
        cur_char = intro_str[i];
        if (cur_char < 0x20)
            continue; // skip invalid characters
        cur_char -= 0x20; // align ascii value with our font array
        for (j = 0; j < 5; j++) // handle each column in character
        {
            screen[cursor] = font5x8[cur_char*5+j];
            cursor++;
        }
        screen[cursor] = 0x00; // kerning gap
        cursor++;
    }

    // show screen data
    refresh_screen();

    if (first_run)
	{
		first_run = 0x00;
		srand((TH2 << 8) | TL2); // seed rand() for better psuedorandomness
	}

    // go into event-driven mode
    while ( 1 )
    {
        // do nothing, wait for interrupt
    }

}
