/* CSCI-4061 Fall 2022
 * Group Member #1: Nicole Vu - vu000166
 * Group Member #2: Jianing Wen - wen00112
 * Group Member #3: Sol Kim - kim01540
 */

#include "wrapper.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>

#define MAX_TABS 100  // this gives us 99 tabs, 0 is reserved for the controller
#define MAX_BAD 1000
#define MAX_URL 100
#define MAX_FAV 100
#define MAX_LABELS 100 


comm_channel comm[MAX_TABS];         // Communication pipes 
char favorites[MAX_FAV][MAX_URL];    // Maximum char length of a url allowed
int num_fav = 0;                     // # favorites
int num_tabs = 0;                    // # active tabs

typedef struct tab_list {
  int free;
  int pid; // may or may not be useful
} tab_list;

// Tab bookkeeping
tab_list TABS[MAX_TABS];  


/************************/
/* Simple tab functions */
/************************/

// return total number of tabs
int get_num_tabs () {
  return num_tabs;
}

// if success, return next free tab index, if fails, return -1
int get_free_tab () {
  for (int i=1; i<MAX_TABS; i++)
    if (TABS[i].free == 1) {
      return i;
    }
  return -1;
}

// init TABS data structure
void init_tabs () {
  int i;

  for (i=1; i<MAX_TABS; i++)
    TABS[i].free = 1;
  TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) {
  // Already on favorite list
  if (on_favorites(uri) == 1){
    alert("Fav exists!");
    return -1;
  }
  // Maximum number of favorites
  if (num_fav >= MAX_FAV){
    alert("Maximum number of favorites!");
    return -1;
  }
  return 0;
}


// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) {
  // Strip http
  char* pass_http;
  if (strncmp("https://", uri, 8) == 0) {
    pass_http = uri + 8;
  } 
  else if (strncmp("http://", uri, 7) == 0) {
    pass_http = uri + 7;
  } 
  else {
    pass_http = uri;
  }

  // Strip www and ending new line character
  char* pass_www;
  if (strncmp("www", pass_http, 3) == 0) {
    pass_www = pass_http + 4;
  }
  else {
    pass_www = pass_http;
  }
  char* noSpace = strtok(pass_www, "\n");


  // Add uri to .favorites file
  FILE *fav_f;

  // Check if fail to open file, else append to file
  if ((fav_f = fopen(".favorites", "a")) == NULL) {
    perror("Failed to open file.");
    return;
  }
  fprintf(fav_f,"%s\n",noSpace);
  
  // Update favorites array with the new favorite
  for(int i =0; i < strlen(noSpace); i++){
    favorites[num_fav][i] = noSpace[i];
  }
  num_fav++;
  fclose(fav_f);  // Close the file
  return;
}

// Set up favorites array
void init_favorites (char *fname) {
  FILE *fav_f;
  // Check if fail to open file, else read file
  if ((fav_f = fopen(fname, "r")) == NULL) {
    perror("Failed to open file.");
    return;
  }

  while(fgets(favorites[num_fav], MAX_URL, fav_f)) {  
    if (num_fav < MAX_FAV) {
      favorites[num_fav][strlen(favorites[num_fav]) - 1] = '\0';
    }
    num_fav++;
  }
  fclose(fav_f);  // Close the file
  return;
}

// Make fd non-blocking, return 0 if ok, -1 otherwise
int non_block_pipe (int fd) {
  int nFlags;
  
  if ((nFlags = fcntl(fd, F_GETFL, 0)) < 0){
    perror("Failed to get flags from fd.");
    return -1;
  }
  if ((fcntl(fd, F_SETFL, nFlags | O_NONBLOCK)) < 0){
    perror("Failed to make pipe non-blocking.");
    return -1;
  }
  return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

void handle_uri (char *uri, int tab_index) {

  // Checks if tab is bad and url violates constraints; if so, return.
  if (on_blacklist(uri) == 1){
    alert("blacklist!");
    return;
  }
  if (bad_format(uri) == 1){
    alert("bad format!");
    return;
  }

  // Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
  req_t req;
  req.type = NEW_URI_ENTERED;
  req.tab_index = tab_index;
  
  strcpy(req.uri, uri);
  write(comm[tab_index].inbound[1], &req, sizeof(req_t));
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent (see Hint)
// Short function
void uri_entered_cb (GtkWidget* entry, gpointer data) {
  // Check if datat is empty
  if(data == NULL) {	
    return;
  }

  // Get the tab (hint: wrapper.h)
  int tabIndex;
  tabIndex = query_tab_id_for_request(entry, data);

  // If there is no active tabs, alert bad tab
  if (tabIndex <= 0 || TABS[tabIndex].free) {
    alert("Bad tab!");
  }
  else {
    // Get the URL (hint: wrapper.h)
    char* enteredUri;
    enteredUri = get_entered_uri(entry);

    printf("URL selected is %s\n", enteredUri);

    // Hint: now you are ready to handle_uri
    handle_uri(enteredUri, tabIndex);
  }
}

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
// Long function
void new_tab_created_cb (GtkButton *button, gpointer data) {
  // check if data is empty
  if (data == NULL) {
    return;
  }

  // Get a free tab & get_free_tab checks tab limit
  if (get_free_tab() == -1){
    alert("Max tab!");
    return;
  }
  int free_tab_id = get_free_tab();
  num_tabs++;

  // Create communication pipes for this tab and check for pipe error
  if (pipe(comm[free_tab_id].inbound) == -1 || pipe(comm[free_tab_id].outbound) == -1) {
    perror("Failed to create pipe for tab.");
    return;
  }

  // Make the read ends non-blocking 
  non_block_pipe(comm[free_tab_id].inbound[0]);
  non_block_pipe(comm[free_tab_id].outbound[0]);


  // fork and create new render tab
  pid_t tab_pid = fork();
  // Fork failed, no tab is created
  if (tab_pid == -1) {
    perror("Failed to fork tab process.");
  }
  // Fork success, run child tab
  // Note: render has different arguments now: tab_index, both pairs of pipe fd's
  // (inbound then outbound) -- this last argument will be 4 integers "a b c d"
  // Hint: stringify args
  else if (tab_pid == 0) {
    // Stringfy the pipe ends
    char pipe_str[20];
    sprintf(pipe_str, "%d %d %d %d", comm[free_tab_id].inbound[0], comm[free_tab_id].inbound[1],
      comm[free_tab_id].outbound[0], comm[free_tab_id].outbound[1]);
    
    // Convert the index of the free tab into string type
    char index_string[10];
    sprintf(index_string, "%d", free_tab_id);

    if (execl("./render", "render", index_string, pipe_str, NULL) == -1){
      printf("Failed to execl().");
    }
  }
  // Fork success, run controller parent
  // Controller parent just does some TABS bookkeeping
  else {
    TABS[free_tab_id].free = 0;
  }


}

// This is called when a favorite is selected for rendering in a tab
// Hint: you will use handle_uri ...
// However: you will need to first add "https://" to the uri so it can be rendered
// as favorites strip this off for a nicer looking menu
// Short
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) {

  if (data == NULL) {
    return;
  }
  
  // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
  // get basic uri
  char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));

  // append "https://" for rendering
  char uri[MAX_URL];
  sprintf(uri, "https://%s", basic_uri);

  // Get the tab (hint: wrapper.h)
  int tabIndex;
  tabIndex = query_tab_id_for_request(menu_item, data);

  // Check if the curernt tab is not active
  if (tabIndex <= 0 || TABS[tabIndex].free) {
    alert("Bad tab!");
  }
  else {
    // Hint: now you are ready to handle_uri
    handle_uri(uri, tabIndex);
  }
  return;
}

// BIG CHANGE: the controller now runs an loop so it can check all pipes
// Long function
int run_control() {
  browser_window * b_window = NULL;
  int i, nRead;
  req_t req;

  //Create controller window
  create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
		 G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

  // Create favorites menu
  create_browser_menu(&b_window, &favorites, num_fav);
  
  while (1) {
    process_single_gtk_event();

    // Read from all tab pipes including private pipe (index 0)
    // to handle commands:
    // PLEASE_DIE (controller should die, self-sent): send PLEASE_DIE to all tabs
    // From any tab:
    //    IS_FAV: add uri to favorite menu (Hint: see wrapper.h), and update .favorites
    //    TAB_IS_DEAD: tab has exited, what should you do?

    // Loop across all pipes from VALID tabs -- starting from 0
    for (i=0; i<MAX_TABS; i++) {
      // Skip the free tabs
      if (TABS[i].free) continue; 

      nRead = read(comm[i].outbound[0], &req, sizeof(req_t));

      // Check if fail to read
      if (nRead == -1) {
        if (errno != EAGAIN) {
          perror("Fail to read.");
          exit(-1);
        }
        continue;
      }
      // Check that nRead returned something before handling cases
      else if (nRead == 0) {
        continue;
      }
      else {
        // Case 1: PLEASE_DIE
        if (req.type == PLEASE_DIE) {
          
          // send PLEASE_DIE to all the active tabs
          for (i=1; i<MAX_TABS; i++) {
            // Skip the free tabs
            if (TABS[i].free) continue;

            // Update the request tab_index and write to the inbound pipes
            req.tab_index = i;
            write(comm[i].inbound[1], &req, sizeof(req_t));
          }

          for (i=1; i<MAX_TABS; i++){
            // Skip the free tabs 
            if (TABS[i].free) continue;
            // Wait for the active tabs
            if (wait(NULL) == -1) {
              perror("Fail to wait");
              exit(-1);
            } 
          }

          // Controller exits when all tabs are closed
          exit(0); 
        }

        // Case 2: TAB_IS_DEAD
        // When TAB_IS_DEAD recieved, wait and free the tab 
        if (req.type == TAB_IS_DEAD) { 
          if (wait(NULL) == -1) {
            perror("Fail to wait");
            exit(-1);
          }

          // Free the tab that sent the message
          TABS[req.tab_index].free = 1;
          num_tabs--;
        }

        // Case 3: IS_FAV
        else if (req.type == IS_FAV) {
          if (fav_ok(req.uri) == 0) {
            update_favorites_file(req.uri);
            add_uri_to_favorite_menu (b_window, req.uri);
          }
          break;
        }

        // Check if the req.type is invalid
        else {
          perror("Invalid request type.");
          break;
        }
      }
    }
    usleep(1000);
  }
  return 0;
}


int main(int argc, char **argv)
{

  if (argc != 1) {
    fprintf (stderr, "browser <no_args>\n");
    exit (0);
  }

  init_tabs ();

  // init blacklist (see util.h), and favorites (write this, see above)
  init_blacklist(".blacklist");
  init_favorites(".favorites");


  // Create pipe between wrapper and controller, check for error if pipe fails
  // Child creates a pipe for itself comm[0]
  if (pipe(comm[0].inbound) == -1 || pipe(comm[0].outbound) == -1) {
    perror ("Failed to create pipe between wrapper and controller.\n");
    return 1;
  }

  // Making the private pipes non-blocking
  non_block_pipe(comm[0].inbound[0]);
  non_block_pipe(comm[0].outbound[0]);

  // Fork controller
  pid_t controller_pid;
  controller_pid = fork();
  // Fork failed, no child is created
  if (controller_pid == -1) {
    perror("Failed to fork the controller.\n"); 
    return 1;
  }
  // Fork success, running controller by calling run_control
  else if (controller_pid == 0) {
    run_control();
  }
  // Fork success, running wrapper
  else {
    // Wait for controller
    if (wait(NULL) == -1) {
      perror("Fail to wait");
      return -1;
    }
    
    // When the controller exits, wrapper sends PLEASE_DIE message to the controller
    req_t wrapper_req;
    wrapper_req.type = PLEASE_DIE;
    write(comm[0].outbound[1], &wrapper_req, sizeof(req_t));

    // Set controller status to free
    TABS[0].free = 1;
  }
  return 0;
}
