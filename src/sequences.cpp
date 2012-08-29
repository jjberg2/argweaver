
#include "common.h"
#include "logging.h"
#include "parsing.h"
#include "seq.h"
#include "sequences.h"



namespace arghmm {


//=============================================================================
// input/output: FASTA

bool read_fasta(FILE *infile, Sequences *seqs)
{
    // store lines until they are ready to discard
    class Discard : public vector<char*> {
    public:
        ~Discard() { clean(); }
        void clean() {
            for (unsigned int i=0; i<size(); i++)
                delete [] at(i);
            clear();
        }
    };
    
    
    // init sequences
    seqs->clear();
    seqs->set_owned(true);

    char *line;    
    string key;
    vector<char*> seq;
    Discard discard;
    
    while ((line = fgetline(infile))) {
        chomp(line);
        
        if (line[0] == '>') {
            // parse key line

            if (seq.size() > 0) {
                // add new sequence
                char *full_seq = concat_strs(&seq[0], seq.size());
                int seqlen2 = strlen(full_seq);
                if (!seqs->append(key, full_seq, seqlen2)) {
                    printError("sequences are not the same length: %d != %d",
                               seqs->length(), seqlen2);
                    delete [] full_seq;
                    return false;
                }
                seq.clear();
                discard.clean();
            }
        
            // new key found
            key = string(&line[1]);
            delete [] line;
        } else {
            // parse sequence line

            seq.push_back(trim(line));
            discard.push_back(line);
        }
    }
    
    // add last sequence
    if (seq.size() > 0) {
        char *full_seq = concat_strs(&seq[0], seq.size());
        int seqlen2 = strlen(full_seq);
        if (!seqs->append(key, full_seq, seqlen2)) {
            printError("sequences are not the same length: %d != %d",
                       seqs->length(), seqlen2);
            delete [] full_seq;
            return false;
        }
        discard.clean();
    }

    // set sequence length
    //seqs->set_length(seqlen);
    
    return true;
}


bool read_fasta(const char *filename, Sequences *seqs)
{
    FILE *infile = NULL;    
    if ((infile = fopen(filename, "r")) == NULL) {
        printError("cannot read file '%s'", filename);
        return false;
    }
    
    bool result = read_fasta(infile, seqs);
    fclose(infile);
    
    return result;
}



bool write_fasta(const char *filename, Sequences *seqs)
{
    FILE *stream = NULL;
    
    if ((stream = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "cannot open '%s'\n", filename);
        return false;
    }

    write_fasta(stream, seqs);
    
    fclose(stream);
    return true;
}

void write_fasta(FILE *stream, Sequences *seqs)
{
    for (int i=0; i<seqs->get_num_seqs(); i++) {
        fprintf(stream, ">%s\n", seqs->names[i].c_str());
        fprintf(stream, "%s\n", seqs->seqs[i]);
    }
}



//=============================================================================
// input/output: sites file format


bool read_sites(FILE *infile, Sites *sites, 
                int subregion_start, int subregion_end)
{
    const char *delim = "\t";
    char *line;
    
    sites->clear();
    bool error = false;
    
    // parse lines
    int lineno = 0;    
    while (!error && (line = fgetline(infile))) {
        chomp(line);
        lineno++;

        if (strncmp(line, "NAMES\t", 6) == 0) {
            // parse NAMES line
            split(&line[6], delim, sites->names);

        } else if (strncmp(line, "REGION\t", 7) == 0) {
            // parse RANGE line
            char chrom[51];
            if (sscanf(line, "REGION\t%50s\t%d\t%d", 
                       chrom,
                       &sites->start_coord, &sites->end_coord) != 3) {
                printError("bad REGION format");
                delete [] line;
                return false;
            }
            sites->chrom = chrom;

            // set region by subregion if specified
            if (subregion_start != -1)
                sites->start_coord = subregion_start;
            if (subregion_end != -1)
                sites->end_coord = subregion_end;
            

        } else if (strncmp(line, "RANGE\t", 6) == 0) {
            // parse RANGE line
            
            printError("deprecated RANGE line detected (use REGION instead)");
            delete [] line;
            return false;

        } else {
            // parse a site line
            
            // parse site
            int position;
            if (sscanf(line, "%d\t", &position) != 1) {
                printError("first column is not an integer (line %d)", lineno);
                delete [] line;
                return false;
            }
            int i=0;
            for (; line[i] != '\t'; i++) {}
            i++;

            if (position < sites->start_coord ||
                position >= sites->end_coord) {
                // skip site if not in region
                delete [] line;
                continue;
            }

            unsigned int len = strlen(&line[i]);
            if (len != sites->names.size()) {
                printError("not enough bases given (line %d)", lineno);
                delete [] line;
                return false;
            }

            sites->append(position, &line[i], true);
        }
        
        delete [] line;
    }
    
    return true;
}



bool read_sites(const char *filename, Sites *sites, 
                int subregion_start, int subregion_end)
{
    FILE *infile;
    if ((infile = fopen(filename, "r")) == NULL) {
        printError("cannot read file '%s'", filename);
        return false;
    }
    
    bool result = read_sites(infile, sites, subregion_start, subregion_end);

    fclose(infile);
    
    return result;
}


void make_sequences_from_sites(const Sites *sites, Sequences *sequences, 
                               char default_char)
{
    int nseqs = sites->names.size();
    int seqlen = sites->length();
    int start = sites->start_coord;
    int nsites = sites->get_num_sites();
    
    //Sequences *sequences = new Sequences(seqlen);
    sequences->clear();
    sequences->set_owned(true);
    
    for (int i=0; i<nseqs; i++) {
        char *seq = new char [seqlen];
        
        int col = 0;
        for (int j=0; j<seqlen; j++) {
            if (col < nsites && start+j == sites->positions[col]) {
                // variant site
                seq[j] = sites->cols[col++][i];
            } else {
                seq[j] = default_char;
            }
        }

        sequences->append(sites->names[i], seq);
    }

    sequences->set_length(seqlen);
}


// compress the sites by a factor of 'compress'
void find_compress_cols(const Sites *sites, int compress, 
                        SitesMapping *sites_mapping)
{
    const int ncols = sites->get_num_sites();

    int blocki = 0;
    int next_block = sites->start_coord + compress;
    int half_block = compress / 2;
    
    // record old coords
    sites_mapping->init(sites);
    
    for (int i=0; i<ncols; i++) {
        int col = sites->positions[i];

        while (col >= next_block) {
            sites_mapping->all_sites.push_back(next_block - half_block);
            next_block += compress;
            blocki++;
        }

        sites_mapping->old_sites.push_back(col);
        sites_mapping->new_sites.push_back(blocki);
        sites_mapping->all_sites.push_back(col);
        next_block += compress;
        blocki++;
    }

    // record non-variants at end of alignment
    while (sites->end_coord >= next_block) {
        sites_mapping->all_sites.push_back(next_block - half_block);
        next_block += compress;
        blocki++;
    }


    // record new coords
    sites_mapping->new_start = 0;
    int new_end = sites->length() / compress;
    if (ncols > 0)
        sites_mapping->new_end = max(sites_mapping->new_sites[ncols-1]+1, 
                                     new_end);
    else
        sites_mapping->new_end = new_end;
}


void compress_sites(Sites *sites, const SitesMapping *sites_mapping)
{
    const int ncols = sites->cols.size();
    sites->start_coord = sites_mapping->new_start;
    sites->end_coord = sites_mapping->new_end;
    
    for (int i=0; i<ncols; i++)
        sites->positions[i] = sites_mapping->new_sites[i];
}


//=============================================================================
// assert functions

bool check_sequences(Sequences *seqs)
{
    return check_sequences(
        seqs->get_num_seqs(), seqs->length(), seqs->get_seqs()) &&
        check_seq_names(seqs);
}

// ensures that all characters in the alignment are sensible
// TODO: do not change alignment (keep Ns)
bool check_sequences(int nseqs, int seqlen, char **seqs)
{
    // check seqs
    // CHANGE N's to gaps
    for (int i=0; i<nseqs; i++) {
        for (int j=0; j<seqlen; j++) {
            char x = seqs[i][j];
            if (strchr("NnRrYyWwSsKkMmBbDdHhVv", x))
                // treat Ns as gaps
                x = '-';
            if (x != '-' &&
                dna2int[(int) (unsigned char) x] == -1)
            {
                // an unknown character is in the alignment
                printError("unknown char '%c' (char code %d)\n", x, x);
                return false;
            }
        }
    }
    
    return true;
}


bool check_seq_names(Sequences *seqs)
{
    for (unsigned int i=0; i<seqs->names.size(); i++) {
        if (!check_seq_name(seqs->names[i].c_str())) {
            printError("sequence name has illegal characters '%s'",
                       seqs->names[i].c_str());
            return false;
        }
    }

    return true;
}


//
// A valid gene name and species name follows these rules:
//
// 1. the first and last characters of the ID are a-z A-Z 0-9 _ - .
// 2. the middle characters can be a-z A-Z 0-9 _ - . or the space character ' '.
// 3. the ID should not be purely numerical characters 0-9
// 4. the ID should be unique within a gene tree or within a species tree
//

bool check_seq_name(const char *name)
{
    int len = strlen(name);
    
    if (len == 0) {
        printError("name is zero length");
        return false;
    }

    // check rule 1
    if (name[0] == ' ' || name[len-1] == ' ') {
        printError("name starts or ends with a space '%c'");
        return false;
    }

    // check rule 2
    for (int i=0; i<len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ' ')) {
            printError("name contains illegal character '%c'", c);
            return false;
        }
    }

    // check rule 3
    int containsAlpha = false;
    for (int i=0; i<len; i++) {
        if (name[i] < '0' || name[i] > '9') {
            containsAlpha = true;
            break;
        }
    }
    if (!containsAlpha) {
        printError("name is purely numeric '%s'", name);
        return false;
    }

    return true;
}


//=============================================================================
// Misc

void resample_align(Sequences *aln, Sequences *aln2)
{
    assert(aln->get_num_seqs() == aln2->get_num_seqs());
    char **seqs = aln->get_seqs();
    char **seqs2 = aln2->get_seqs();

    for (int j=0; j<aln2->length(); j++) {
        // randomly choose a column (with replacement)
        int col = irand(aln->length());
        
        // copy column
        for (int i=0; i<aln2->get_num_seqs(); i++) {
            seqs2[i][j] = seqs[i][col];
        }
    }
}

} // namespace arghmm
