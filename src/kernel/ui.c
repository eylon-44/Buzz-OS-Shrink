// UI Manager // ~ eylon

#include <kernel/ui.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/vmm.h>
#include <kernel/memory/pmm.h>
#include <kernel/process/pm.h>
#include <kernel/process/scheduler.h>
#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <libc/sys/syscall.h>
#include <libc/unistd.h>
#include <libc/string.h>
#include <libc/ctype.h>
#include <libc/list.h>

static void shortcut_handler_tab_switch(char key);
static void shortcut_handler_tab_open(char key);
static void shortcut_handler_tab_close(char key);


static tab_list_t tabs = {.tab_list=NULL, .active=NULL, .active_index=0, .count=0};
static const char tab_indexes[UI_MAX_TABS+1] = "1234567890qwertyuiopasdfghjkl;zxcvbnm,./";
static char stdin_buff[UI_MAX_IN];
// Shortcut list
static struct
{
    char key;
    uint8_t modifiers;
    void (*handler)(char key);
}
shortcuts[] = {
    {'\0', KB_MFLAG_ALT, shortcut_handler_tab_switch},
    {'t', KB_MFLAG_CTRL | KB_MFLAG_SHIFT, shortcut_handler_tab_open},
    {'w', KB_MFLAG_CTRL | KB_MFLAG_SHIFT, shortcut_handler_tab_close}
};

// Get the list index of a tab by its header index
static int get_index_by_char(char chr_indx)
{    
    int lst_indx = (int) strchr(tab_indexes, chr_indx) - (int) tab_indexes;
    if (lst_indx > UI_MAX_TABS) {
        return -1;
    }
    return lst_indx;
}

// Get a pointer to a tab by its screen header index
static tab_t* get_tab_by_index(char index)
{
    tab_t* tab = tabs.tab_list;
    int list_index = get_index_by_char(index);

    if (list_index < 0) {
        return NULL;
    }

    // Find the tab at the given index
    while (list_index > 0)
    {
        tab = tab->next;
        list_index--;
    }

    return tab;
}

// Print the screen header
static void update_header()
{
    // Go over all tab indexes
    for (int i = 0; i < UI_MAX_TABS; i++)
    {
        if (i < tabs.count) {
            // Print active tab
            if (tab_indexes[i] == tabs.active_index) {
                vga_put_char_at(tab_indexes[i], UI_ATR_TAB_SELECTED, i*2);
            }
            // Print opened tab
            else {
                vga_put_char_at(tab_indexes[i], UI_ATR_TAB_OPEN, i*2);
            }
        }
        // Print closed tab
        else {
            vga_put_char_at(tab_indexes[i], UI_ATR_TAB_CLOSE, i*2);
        }

        // Space the indexing
        vga_put_char_at(' ', UI_ATR_TAB_CLOSE, i*2+1);
    }
}

/* Scroll the screen down (move the text up) so it could fit [count] bytes and return the new offset.
    [buff_out] is the screen buffer; could be the VGA screen or any other stdout buffer.
    [offset] is the current cursor location on the buffer.
    [count] is the number of bytes we try to fit in the screen by scrolling. */
static size_t handle_scrolling(char* buff_out, int offset, size_t count)
{
    // If print is going out of screen bounds
    while (offset+count >= UI_MAX_OUT) {
        memcpy(buff_out, buff_out + VGA_COL_COUNT * 2, (VGA_SIZE - VGA_COL_COUNT)*2);
        // blank the last line
        for (uint8_t i = 0; i < VGA_COL_COUNT; i++) {
            vga_put_char_at(' ', UI_ATR_DEFAULT, (VGA_SIZE - VGA_COL_COUNT + i));
        }
        offset -= (VGA_COL_COUNT);
    }
    if (offset < 0) {
        return 0;
    }
    return offset;
}


// Shortcut handler
static void shortcut_handler_tab_open(UNUSED char key) { ui_tab_open(); }
static void shortcut_handler_tab_close(UNUSED char key) { ui_tab_close(); }
static void shortcut_handler_tab_switch(char key) { ui_tab_switch(key); }
static void handle_shortcut(char key, uint8_t modifiers)
{
    // Iterate over the shortcut list
    for (size_t i = 0; i < sizeof(shortcuts)/sizeof(shortcuts[0]); i++)
    {
        if ((modifiers & shortcuts[i].modifiers) && (shortcuts[i].key == '\0' || shortcuts[i].key == tolower(key))) {
            shortcuts[i].handler(key);
        }
    }
}

// Keyboard event handler
void ui_key_event_handler(char key, uint8_t modifiers)
{
    // If there are no open tabs, return
    if (tabs.count <= 0) {
        return;
    }

    // If event is a shortcut, pass it to the shorcut handler and return
    if (modifiers & (KB_MFLAG_CTRL | KB_MFLAG_ALT)) {
        handle_shortcut(key, modifiers);
        return;
    }

    // If backspace was pressed
    if (key == KEY_BACKSPACE) {
        // If buffer is not empty
        if (tabs.active->in_offset > 0)
        {
            // Delete the last character
            ui_cursor_set(tabs.active, ui_cursor_get(tabs.active)-1);
            vga_put_char_at(' ', UI_ATR_DEFAULT, ui_cursor_get(tabs.active) + VGA_COL_COUNT);
            // Decrease the offset
            tabs.active->in_offset--;
        }
        return;
    }
    // If return was pressed
    if (key == KEY_RETURN) {
        if (tabs.active->flags & TABF_TAKING_INPUT) {
            sched_set_status(tabs.active->parnet, PSTATUS_READY);
        }
        return;
    }

    // If buffer is full
    if (tabs.active->in_offset >= UI_MAX_IN) {
        return;
    }

    // Add key to the buffer
    stdin_buff[tabs.active->in_offset] = key;
    tabs.active->in_offset++;

    // Print the key ???with stdout????
    ui_cursor_set(tabs.active, handle_scrolling((char*) UI_SCREEN_BUFF, ui_cursor_get(tabs.active), 1));
    vga_put_char(key, UI_ATR_DEFAULT);
}

/* Read [count] bytes from the stdin buffer to [buff].
    The function blocks the calling process until an stdin flush. On that flush
    it copies no more than [count] bytes from the stdin buffer into [buff] and
    returns the number of bytes read. */
size_t ui_stdin_read(const char* buff, size_t count)
{
    tab_t* tab;
    size_t input_size;   

    // Get caller's tab and reset its stdin buffer if needed
    tab = pm_get_active()->tab;

    // Reset input buffer and mark tab as taking input
    tab->in_offset=0;
    tab->flags |= TABF_TAKING_INPUT;
    
    // Block this context; control will return once stdin flushes
    sched_set_status(pm_get_active(), PSTATUS_BLOCKED);
    syscall(SYS_sched_yield);
    

    // Calcuate input size
    if ((size_t) tab->in_offset < count) {
        input_size = tab->in_offset;
    }
    else {
        input_size = count;
    }

    // Reset input buffer and mark tab as not taking input
    tab->in_offset = 0;
    tab->flags &= ~TABF_TAKING_INPUT;

    // Copy the input buffer into [buff]
    memcpy((void*) buff, stdin_buff, input_size);

    return input_size;
}

// Write [count] bytes from [buff] to the stdout buffer of the calling process
void ui_stdout_write(const char* buff, size_t count)
{
    // Tab of caller
    tab_t* tab = pm_get_active()->tab;

    // If the tab is the active tab, write directly to the screen
    if (tab == tabs.active) {
        // Scroll the screen as needed        
        ui_cursor_set(tab, handle_scrolling((char*) UI_SCREEN_BUFF, ui_cursor_get(tab), count));
        // Print the string to the screen
        vga_print_n(buff + count - (count%UI_MAX_OUT), UI_ATR_DEFAULT, count % UI_MAX_OUT);
    }
    // If not, write to its buffer
    else {
        // Temporarlily attach the buffer of the target tab
        tab_buff_t* tab_buff = (tab_buff_t*) vmm_attach_page((paddr_t) tab->buff);

        // Scroll the screen as needed
        tab->out_offset = handle_scrolling(tab_buff->out, tab->out_offset, count);
        // Copy add the string into the buffer
        memcpy(tab_buff->out, buff + count - (count%UI_MAX_OUT), count % UI_MAX_OUT);

        vmm_detach_page((vaddr_t) tab_buff);
    }
}

// Set the cursor for the given tab
void ui_cursor_set(tab_t* tab, size_t offset)
{
    // If it's the active tab
    if (tabs.active == tab) {
        vga_set_cursor(offset + VGA_COL_COUNT);
    }
    else {
        tab->out_offset = offset;
    }
    
}

// Set the cursor of the given tab
size_t ui_cursor_get(tab_t* tab)
{
    // If it's the active tab
    if (tabs.active == tab) {
        return vga_get_cursor() - VGA_COL_COUNT;
    }
    else {
        return tab->out_offset;
    }
}

// Open a new tab with the default program and switch to it
void ui_tab_open()
{
    tab_t* tab;
    tab_buff_t* buff;
    process_t* terminal;

    // Check we don't open too much tabs
    if (tabs.count >= UI_MAX_TABS) {
        return;
    }

    // Allocate and set the new tab
    tab = (tab_t*) kmalloc(sizeof(tab_t));
    tab->out_offset = 0;
    tab->in_offset  = 0;
    tab->flags      = 0;
    tab->buff       = (tab_buff_t*) pmm_get_page();

    // Create a new terminal process
    terminal = pm_load(NULL, UI_DEFAULT_TERMINAL, 20);
    terminal->tab   = tab;
    tab->parnet     = terminal;

    // Clear stdout buffer
    buff = (tab_buff_t*) vmm_attach_page((paddr_t) tab->buff);
    memset(buff->out, ' ', UI_MAX_OUT);
    vmm_detach_page((vaddr_t) buff);

    // Add the tab to the list
    LIST_ADD_END(tabs.tab_list, tab);
    tabs.count++;

    // Switch to the new tab
    ui_tab_switch(tab_indexes[tabs.count-1]);
}

// Close the displayed tab
void ui_tab_close()
{
    pm_kill(tabs.active->parnet);

    // Free tab resources and remove it from the list
    pmm_free_page((paddr_t) tabs.active->buff);
    LIST_REMOVE(tabs.tab_list, tabs.active);
    kfree(tabs.active);
    tabs.active = NULL;

    // Decrease tab count
    tabs.count--;

    // If there are open tabs, switch to the tab that is on the left, or to the tab that is on the right if this is the last tab
    if (tabs.count > 0) {
        // If there is a tab to the left of the one we closed, switch to it
        if (get_index_by_char(tabs.active_index) < tabs.count) {
            ui_tab_switch(tabs.active_index);
        }
        // If not, switch to the tab on the left.
        else {
            ui_tab_switch(tab_indexes[get_index_by_char(tabs.active_index)-1]);
        }
    }
    // Else, open a new tab
    else {
        ui_tab_open();
    }
}

// Switch displayed tab
void ui_tab_switch(char index)
{
    tab_t* tab = get_tab_by_index(index);
    tab_buff_t* buff;

    // If there is no tab at the given index, or switching to the active tab, return
    if (tab == NULL || tab == tabs.active) {
        return;
    }

    // If there is an open tab, save its contents before switching
    if (tabs.active != NULL) {
        // Save the stdin and stdout buffer
        buff = (tab_buff_t*) vmm_attach_page((paddr_t) tabs.active->buff);
        for (int i = 0; i < UI_MAX_OUT; i++) {
            buff->out[i] = ((char*) UI_SCREEN_BUFF)[i*2];
        }
        memcpy(buff->in, stdin_buff, tabs.active->in_offset+1);
        vmm_detach_page((vaddr_t) buff);

        // Save the cursor
        tabs.active->out_offset = ui_cursor_get(tabs.active);
    }

    // Set the tab as active
    tabs.active       = tab;
    tabs.active_index = index;

    // Set the cursor
    ui_cursor_set(tabs.active, tab->out_offset);

    // Temporarlily attach the buffer of the target tab
    buff = (tab_buff_t*) vmm_attach_page((paddr_t) tab->buff);

    // Copy the stdout buffer to the screen
    for (int i = 0; i < UI_MAX_OUT; i++) {
        ((char*) UI_SCREEN_BUFF)[i*2]   = buff->out[i];
        ((char*) UI_SCREEN_BUFF)[i*2+1] = UI_ATR_DEFAULT;
    }

    // Copy the stdin buffer into the local buffer
    memcpy(stdin_buff, buff->in, tabs.active->in_offset+1);

    // Detach the buffer and update the header
    vmm_detach_page((vaddr_t) buff);
    update_header();
}

// Initiate the UI module
void init_ui()
{
    vga_clear();
    ui_tab_open();
}