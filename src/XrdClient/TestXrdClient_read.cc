#include "XrdClient/XrdClient.hh"
#include "XrdClient/XrdClientEnv.hh"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sys/time.h>
#include <math.h>

int ReadSome(kXR_int64 *offs, kXR_int32 *lens, int maxnread, long long &totalbytes) {

    for (int i = 0; i < maxnread;) {

	lens[i] = -1;
	offs[i] = -1;
	
	if (cin.eof()) return i;

	cin >> lens[i] >> offs[i];

	if ((lens[i] > 0) && (offs[i] >= 0)) {
	  totalbytes += lens[i];
	  i++;
	}

    }

    return maxnread;

}

// Waste cpu cycles for msdelay milliseconds
void Think(long msdelay) {

    timeval tv;
    long long tlimit, t;

    if (msdelay <= 0) return;

    gettimeofday(&tv, 0);
    tlimit = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000 + msdelay;
    t = 0;

    while ( t < tlimit ) {

	double numb[1000];
	for (int i = 0; i < 100; i++)
	    numb[i] = random();

	for (int i = 0; i < 100; i++)
	    numb[i] = sqrt(numb[i]);
	
	for (int i = 0; i < 100; i++)
	    memmove(numb+10, numb, 90*sizeof(double));

	gettimeofday(&tv, 0);
	t = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }


}


int main(int argc, char **argv) {
    void *buf;
    int vectored_style = 0;
    long read_delay = 0;
    timeval tv;
    double starttime = 0, openphasetime = 0, endtime = 0, closetime = 0;
    long long totalbytesread = 0, prevtotalbytesread = 0;
    long totalreadscount = 0;
    int filezcount = 0;
    string summarypref = "$$$";
    bool iserror = false;
    bool dobytecheck = false;

    gettimeofday(&tv, 0);
    starttime = tv.tv_sec + tv.tv_usec / 1000000.0;
    closetime = openphasetime = starttime;


    if (argc < 2) {
	cout << endl << endl <<
	    "This program gets from the standard input a sequence of" << endl <<
	    " <length> <offset>             (one for each line, with <length> less than 16M)" << endl <<
	    " and performs the corresponding read requests towards the given xrootd URL or to ALL" << endl <<
	    " the xrootd URLS contained in the given file." << endl <<
 	    endl <<
	    "Usage: TestXrdClient_read <xrootd url or file name> <blksize> <cachesize> <vectored_style>  <inter_read_delay_ms> [--check] [-DSparmname stringvalue]... [-DIparmname intvalue]..." <<
	    endl << endl <<
	    " Where:" << endl <<
	    "  <xrootd url>          is the xrootd URL of a remote file " << endl <<
	    "  <rasize>              is the cache block size. Can be 0." << endl <<
	    "  <cachesize>           is the size of the internal cache, in bytes. Can be 0." << endl <<
	    "  <vectored_style>      means 0: no vectored reads (default)," << endl <<
	    "                              1: sync vectored reads," << endl <<
	    "                              2: async vectored reads, do not access the buffer," << endl <<
	    "                              3: async vectored reads, copy the buffers" << endl <<
	    "                                (makes it sync through async calls!)" << endl <<
	    "                              4: no vectored reads. Async reads followed by sync reads." << endl <<
	    "                                (exploits the multistreaming for single reads)" << endl <<
	    "  <inter_read_delay_ms> is the optional think time between reads." << endl <<
	    "                        note: the think time will comsume cpu cycles, not sleep." << endl <<
            "  --check               verify if the value of the byte at offet i is i%256. Valid only for the single url mode." << endl <<
	    " -DSparmname stringvalue" << endl <<
	    "                        set the internal parm <parmname> with the string value <stringvalue>" << endl <<
   	    "                         See XrdClientConst.hh for a list of parameters." << endl <<
	    " -DIparmname intvalue" << endl <<
            "                        set the internal parm <parmname> with the integer value <intvalue>" << endl <<
            "                         See XrdClientConst.hh for a list of parameters." << endl <<
	    "                         Examples: -DSSocks4Server 123.345.567.8 -DISocks4Port 8080 -DIDebugLevel 1" << endl;
	exit(1);
    }

    if (argc > 2)
      EnvPutInt( NAME_READAHEADSIZE, atol(argv[2]));

    if (argc >= 3)
      EnvPutInt( NAME_READCACHESIZE, atol(argv[3]));
   
    if (argc > 4)
	vectored_style = atol(argv[4]);

    if (argc > 5)
	read_delay = atol(argv[5]);

    // The other args, they have to be an even number. Odd only if --check is there
    if (argc > 6)
      for (int i=6; i < argc; i++) {

	if (strstr(argv[i], "--check") == argv[i]) {
	  cerr << "Enabling file content check." << endl;
	  dobytecheck = true;
	  continue;
	}

	if ( (strstr(argv[i], "-DS") == argv[i]) &&
	     (argc >= i+2) ) {
	  cerr << "Overriding " << argv[i]+3 << " with value " << argv[i+1] << ". ";
	  EnvPutString( argv[i]+3, argv[i+1] );
	  cerr << " Final value: " << EnvGetString(argv[i]+3) << endl;
	  i++;
	  continue;
	}

	if ( (strstr(argv[i], "-DI") == argv[i]) &&
	     (argc >= i+2) ) {
	  cerr << "Overriding '" << argv[i]+3 << "' with value " << argv[i+1] << ". ";
	  EnvPutInt( argv[i]+3, atoi(argv[i+1]) );
	  cerr << " Final value: " << EnvGetLong(argv[i]+3) << endl;
	  i++;
	  continue;
	}

      }

    buf = malloc(200*1024*1024);

    // Check if we have a file or a root:// url
    bool isrooturl = (strstr(argv[1], "root://"));
    int retval = 0;
    int ntoread = 0;
    int maxtoread = 10240;
    kXR_int64 v_offsets[10240];
    kXR_int32 v_lens[10240];

    switch (vectored_style) {
      case 0:
        maxtoread = 1;
	break;
	//      case 4:
	//        maxtoread = 20;
	//       break;
    }

    if (isrooturl) {
	XrdClient *cli = new XrdClient(argv[1]);

	cli->Open(0, 0);
	filezcount = 1;

	gettimeofday(&tv, 0);
	openphasetime = tv.tv_sec + tv.tv_usec / 1000000.0;

	while ( (ntoread = ReadSome(v_offsets, v_lens, maxtoread, totalbytesread)) ) {
	  cout << ".";

	  totalreadscount += ntoread;

	    switch (vectored_style) {
	    case 0: // no readv
		for (int iii = 0; iii < ntoread; iii++) {
		    retval = cli->Read(buf, v_offsets[iii], v_lens[iii]);
		    cout.flush();

		    if (retval <= 0) {
			cout << endl << "---Read (" << iii << " of " << ntoread << " " <<
			    v_lens[iii] << "@" << v_offsets[iii] <<
			    " returned " << retval << endl;
			
			iserror = true;
			break;
		    }
		    else {

		      if (dobytecheck)
			for ( unsigned int jj = 0; jj < (unsigned)v_lens[iii]; jj++) {
			  
			  if ( ((jj+v_offsets[iii]) % 256) != ((unsigned char *)buf)[jj] ) {
			    cout << "errore nel file all'offset= " << jj+v_offsets[iii] <<
			      " letto: " << (int)((unsigned char *)buf)[jj] << " atteso: " << (jj+v_offsets[iii]) % 256 << endl;
			    iserror = true;
			    break;
			  }
		      }
		      Think(read_delay);
		    }

		}		
		break;
	    case 1: // sync
		retval = cli->ReadV((char *)buf, v_offsets, v_lens, ntoread);
		cout << endl << "---ReadV returned " << retval << endl;

		if (retval > 0) Think(read_delay * ntoread);
		else {
		  iserror = true;
		  break;
		}

		break;
		
	    case 2: // async
		retval = cli->ReadV(0, v_offsets, v_lens, ntoread);
		cout << endl << "---ReadV returned " << retval << endl;
		break;
		
	    case 3: // async and immediate read, optimized!
		
		for (int ii = 0; ii < ntoread; ii+=512) {

		    // Read a chunk of data
		    retval = cli->ReadV(0, v_offsets+ii, v_lens+ii, xrdmin(ntoread - ii, 512) );
		    cout << endl << "---ReadV returned " << retval << endl;
		    
		    if (retval <= 0) {
		      iserror = true;
		      break;
		    }

		    // Process the preceeding chunk while the last is coming
		    for (int iii = ii-512; (iii >= 0) && (iii < ii); iii++) {
			retval = cli->Read(buf, v_offsets[iii], v_lens[iii]);

			cout.flush();

			if (retval <= 0)
			    cout << endl << "---Read (" << iii << " of " << ntoread << " " <<
				v_lens[iii] << "@" << v_offsets[iii] <<
				" returned " << retval << endl;

			if (dobytecheck)
			  for ( unsigned int jj = 0; jj < (unsigned)v_lens[iii]; jj++) {
			    if ( ((jj+v_offsets[iii]) % 256) != ((unsigned char *)buf)[jj] ) {
			      cout << "errore nel file all'offset= " << jj+v_offsets[iii] <<
				" letto: " << (int)((unsigned char *)buf)[jj] << " atteso: " << (jj+v_offsets[iii]) % 256 << endl;
			      iserror = true;
			      break;
			    }
			  }
			
			Think(read_delay);
		    }
		    
		}
			    
		retval = 1;

		break;
		
	    
    	    case 4: // read async and then read
		for (int iii = 0; iii < ntoread; iii++) {
		    retval = cli->Read_Async(v_offsets[iii], v_lens[iii]);
	  
		    if (retval <= 0) {
			cout << endl << "---Read_Async (" << iii << " of " << ntoread << " " <<
			    v_lens[iii] << "@" << v_offsets[iii] <<
			    " returned " << retval << endl;
			iserror = true;
			break;
		    }

		}		
		for (int iii = 0; iii < ntoread; iii++) {
  		    retval = cli->Read(buf, v_offsets[iii], v_lens[iii]);
		    cout.flush();

		    if (retval <= 0)
			cout << endl << "---Read (" << iii << " of " << ntoread << " " <<
			    v_lens[iii] << "@" << v_offsets[iii] <<
			    " returned " << retval << endl;
	  
		    Think(read_delay);
		}



		break;

	    }

	    if (!cli->IsOpen_wait()) {
	      iserror = true;
	      break;
	    }

	} // while

	gettimeofday(&tv, 0);
	closetime = tv.tv_sec + tv.tv_usec / 1000000.0;

	cli->Close();
	delete cli;
	cli = 0;


    }
    else {
      // Same test on multiple filez

      vector<XrdClient *> xrdcvec;
      ifstream filez(argv[1]);
      int i = 0, fnamecount = 0;;
      XrdClientUrlInfo u;

      // Open all the files (in parallel man!)
      while (!filez.eof()) {
	string s;
	XrdClient * cli;

	filez >> s;
	if (s != "") {
	  fnamecount++;
	  cli = new XrdClient(s.c_str());
	  u.TakeUrl(s.c_str());
	      
	  if (cli->Open(0, 0)) {
	    cout << "--- Open of " << s << " in progress." << endl;
	    xrdcvec.push_back(cli);
	  }
	  else delete cli;
	}
	    
	i++;
      }

      filez.close();

      filezcount = xrdcvec.size();
      cout << "--- All the open requests have been submitted" << endl;

      if (fnamecount == filezcount) {
     
	i = 0;


	gettimeofday(&tv, 0);
	openphasetime = tv.tv_sec + tv.tv_usec / 1000000.0;

	while ( (ntoread = ReadSome(v_offsets, v_lens, 10240, totalbytesread)) ) {


	  switch (vectored_style) {
	  case 0: // no readv
	    for (int iii = 0; iii < ntoread; iii++) {


	      for(int i = 0; i < (int) xrdcvec.size(); i++) {

		retval = xrdcvec[i]->Read(buf, v_offsets[iii], v_lens[iii]);

		cout.flush();

		if (retval <= 0) {
		  cout << endl << "---Read (" << iii << " of " << ntoread << ") " <<
		    v_lens[iii] << "@" << v_offsets[iii] <<
		    " returned " << retval << endl;		 
		  iserror = true;
		  break;
		}


		if (retval > 0) {

		      if (dobytecheck)
			for ( unsigned int jj = 0; jj < (unsigned)v_lens[iii]; jj++) {
			  
			  if ( ((jj+v_offsets[iii]) % 256) != ((unsigned char *)buf)[jj] ) {
			    cout << "errore nel file all'offset= " << jj+v_offsets[iii] <<
			      " letto: " << (int)((unsigned char *)buf)[jj] << " atteso: " << (jj+v_offsets[iii]) % 256 << endl;
			    iserror = true;
			    break;
			  }
		      }

		  Think(read_delay);
		}

	      }

	    }
		
	    break;
	  case 1: // sync

	    for(int i = 0; i < (int) xrdcvec.size(); i++) {

	      retval = xrdcvec[i]->ReadV((char *)buf, v_offsets, v_lens, ntoread);

	    
	      cout << endl << "---ReadV " << xrdcvec[i]->GetCurrentUrl().GetUrl() <<
                " of " << ntoread << " chunks " <<
		" returned " << retval << endl;

	      if (retval) {
                cout << "start think " << time(0) << endl;
                for (int kkk = 0; kkk < ntoread; kkk++) Think(read_delay);
                cout << time(0) << endl;
              }
	      else {
		iserror = true;
		break;
	      }

	    }

	    break;
		
	  case 2: // async

	    for(int i = 0; i < (int) xrdcvec.size(); i++) {

	      retval = xrdcvec[i]->ReadV((char *)0, v_offsets, v_lens, ntoread);
	      cout << endl << "---ReadV " << xrdcvec[i]->GetCurrentUrl().GetUrl() <<
		" returned " << retval << endl;
	    }

	    break;
		
	  case 3: // async and immediate read, optimized!
		
	    for (int ii = 0; ii < ntoread; ii+=4096) {

	      // Read a chunk of data
	      for(int i = 0; i < (int) xrdcvec.size(); i++) {

		retval = xrdcvec[i]->ReadV((char *)0, v_offsets+ii, v_lens+ii, xrdmin(4096, ntoread-ii));
		cout << endl << "---ReadV " << xrdcvec[i]->GetCurrentUrl().GetUrl() <<
                  " of " << xrdmin(4096, ntoread-ii) << " chunks " <<
		  " returned " << retval << endl;

		if (retval <= 0) {
		  iserror = true;
		  break;
		}

	      }

	      // Process the preceeding chunk while the last is coming
	      for (int iii = ii-4096; (iii >= 0) && (iii < ii); iii++) {

		for(int i = 0; i < (int) xrdcvec.size(); i++) {

		  retval = xrdcvec[i]->Read(buf, v_offsets[iii], v_lens[iii]);

		  //cout << ".";
		  //cout.flush();

		  if (retval <= 0)
		    cout << endl << "---Read " << xrdcvec[i]->GetCurrentUrl().GetUrl() <<
		      "(" << iii << " of " << ntoread << ") " <<
		      v_lens[iii] << "@" << v_offsets[iii] <<
		      " returned " << retval << endl;	

		  if (retval > 0) {


		      if (dobytecheck)
			for ( unsigned int jj = 0; jj < (unsigned)v_lens[iii]; jj++) {
			  
			  if ( ((jj+v_offsets[iii]) % 256) != ((unsigned char *)buf)[jj] ) {
			    cout << "errore nel file all'offset= " << jj+v_offsets[iii] <<
			      " letto: " << (int)((unsigned char *)buf)[jj] << " atteso: " << (jj+v_offsets[iii]) % 256 << endl;
			    iserror = true;
			    break;
			  }
		      }

		    Think(read_delay);
		  }

		}

	      }
		    
	    }
			    
	    retval = 1;

	    break;

	  case 4: // read async and then read

	    for (int ii = 0; ii < ntoread; ii+=512) {

	      // Read a chunk of data
	      for(int i = 0; i < (int) xrdcvec.size(); i++) {

		for (int jj = 0; jj < xrdmin(512, ntoread-ii); jj++) {
		  retval =  xrdcvec[i]->Read_Async(v_offsets[ii+jj], v_lens[ii+jj]);

		  // cout << "read_async "  <<
		  //    v_lens[ii+jj] << "@" << v_offsets[ii+jj]<< endl;

		  if (retval != kOK) {
		    cout << endl << "---Read_Async "  << xrdcvec[i]->GetCurrentUrl().GetUrl() <<
		      "(" << ii+jj << " of " << ntoread << ") " <<
		      v_lens[ii+jj] << "@" << v_offsets[ii+jj] <<
		      " returned " << retval << endl;
		    
		    //		    iserror = true;
		    break;
		  }
		}

	      }

	      // Process the preceeding chunk while the last is coming
	      for (int iii = ii-512; (iii >= 0) && (iii < ii); iii++) {

		for(int i = 0; i < (int) xrdcvec.size(); i++) {

		  retval = xrdcvec[i]->Read(buf, v_offsets[iii], v_lens[iii]);

		  //cout << ".";
		  //cout.flush();

		  if (retval > 0) {

		    if (dobytecheck)
		      for ( unsigned int jj = 0; jj < (unsigned)v_lens[iii]; jj++) {
			
			if ( ((jj+v_offsets[iii]) % 256) != ((unsigned char *)buf)[jj] ) {
			  cout << "errore nel file all'offset= " << jj+v_offsets[iii] <<
			    " letto: " << (int)((unsigned char *)buf)[jj] << " atteso: " << (jj+v_offsets[iii]) % 256 << endl;
			  iserror = true;
			  break;
			}
		      }

		    Think(read_delay);
		  }
		  else {
		    cout << endl << "---Read (" << iii << " of " << ntoread << ") " <<
		      v_lens[iii] << "@" << v_offsets[iii] <<
		      " returned " << retval << endl;		 
		    iserror = true;
		    break;
		  }


		  if (iserror) break;

		} // for i

		if (iserror) break;
	      } // for iii
		    
	      if (iserror) break;
	    } // for ii
			    
	    retval = 1;


	    break;

		
	  } // switch

	  if (iserror && prevtotalbytesread) {
	    totalbytesread = prevtotalbytesread;
	    break;
	  }

	  prevtotalbytesread = totalbytesread;
	  totalreadscount += ntoread;
	} // while readsome

	gettimeofday(&tv, 0);
	closetime = tv.tv_sec + tv.tv_usec / 1000000.0;

	cout << endl << endl << "--- Closing all instances" << endl;
	for(int i = 0; i < (int) xrdcvec.size(); i++) {
	  if (xrdcvec[i]->IsOpen()) xrdcvec[i]->Close();
	  else cout << "WARNING: file '" <<
	    xrdcvec[i]->GetCurrentUrl().GetUrl() << " was not opened." << endl;
	}
    
	cout << "--- Deleting all instances" << endl;
	for(int i = 0; i < (int) xrdcvec.size(); i++) delete xrdcvec[i];
     
	cout << "--- Clearing pointer vector" << endl; 
	xrdcvec.clear();


      } //if fnamecount == filezcount



    } // Case of multiple urls
  

    cout << "--- Freeing buffer" << endl;
    free(buf);

    gettimeofday(&tv, 0);
    endtime = tv.tv_sec + tv.tv_usec / 1000000.0;

    if (iserror) summarypref = "%%%";


    cout << "Summary ----------------------------" << endl;
    cout << summarypref << " starttime: " << starttime << endl;
    cout << summarypref << " lastopentime: " << openphasetime << endl;
    cout << summarypref << " closetime: " << closetime << endl;
    cout << summarypref << " endtime: " << endtime << endl;
    cout << summarypref << " open_elapsed: " << openphasetime - starttime << endl;
    cout << summarypref << " data_xfer_elapsed: " << closetime - openphasetime << endl;
    cout << summarypref << " close_elapsed: " << endtime - closetime << endl;
    cout << summarypref << " total_elapsed: " << endtime - starttime << endl;
    cout << summarypref << " totalbytesreadperfile: " << totalbytesread << endl;
    cout << summarypref << " maxbytesreadpersecperfile: " << totalbytesread / (closetime - openphasetime) << endl;
    cout << summarypref << " effbytesreadpersecperfile: " << totalbytesread / (endtime - starttime) << endl;
    cout << summarypref << " readscountperfile: " << totalreadscount << endl;
    cout << summarypref << " openedkofilescount: " << filezcount << endl;
    cout << endl;


    return 0;




}
