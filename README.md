# Router IP in C

Acesta proiect implementeaza un router de retea pe nivelul de date (dataplane) in limbajul C. Routerul primeste pachete Ethernet, verifica header-ul IP, aplica regulile de rutare si trimite pachetele mai departe folosind tabela de rutare, protocolul ARP si mecanisme de control ICMP.

## Scopul proiectului

Tema are ca obiectiv simularea unui router real care:
- proceseaza pachete la nivelul 2 (Ethernet) si 3 (IPv4);
- gaseste cea mai buna ruta prin Longest Prefix Match (LPM);
- rezolva adrese MAC prin ARP;
- gestioneaza erorile de transmisie si TTL expira;
- raspunde la cereri ICMP pentru verificare si eroare.

## Functionalitati implementate

### 1. Forwarding IP
Routerul verifica daca un pachet este valid si il forwardeaza catre interfata potrivita.

Pasi principali sunt:
- verificarea checksum-ului IP;
- decrementarea TTL si validarea acesteia;
- selectarea rutei prin LPM;
- recalcularea checksum-ului dupa modificarea TTL;
- rescrierea adreselor MAC sursa/destinatie conform next-hop-ului.

### 2. Longest Prefix Match (LPM)
Pentru a gasi cea mai buna ruta in mod eficient, am implementat o structura de tip trie (arbore de prefixe):
- fiecare nod are doi copii (0 si 1);
- prefixele din tabela de rutare sunt introduse in arbore bit cu bit;
- cautarea IP-ului de destinatie parcurge cei 32 de biti, pastrand ultima ruta valida intalnita.

Aceasta ofera o complexitate de aproximativ O(32) pentru fiecare cautare, mult mai buna decat o cautare liniara.

### 3. Protocolul ARP
Routerul foloseste ARP pentru a obtine adresele MAC ale vecinilor.

Cand nu exista o intrare in cache pentru next-hop:
- pachetul IP este pus intr-o coada;
- routerul trimite un ARP Request broadcast;
- dupa primirea ARP Reply, adresa MAC este salvata in cache;
- pachetele in asteptare sunt transmise catre destinatia corecta.

Routerul si raspunde la cereri ARP venite de la alte dispozitive.

### 4. Protocolul ICMP
Proiectul include si suport pentru mesaje ICMP:
- Echo Request/Echo Reply;
- Time Exceeded (TTL <= 1);
- Destination Unreachable (ruta absenta);
- alte mesaje de control necesare pentru depanare si testare.

## Structura proiectului

- `router.c` - implementarea principala a routerului;
- `include/` - fisiere header si definitii de structuri;
- `lib/` - biblioteci auxiliare pentru cozi, liste si functii de utilitate;
- `Makefile` - compilare si rulare automata;
- `rtable*.txt` - tabele de rutare pentru scenarii de test.

## Cerinte

- compilator GCC;
- sistem de operare Linux/Unix-like;
- mediul de simulare / testare folosit pentru retea (ex. Mininet, framework-ul laboratorului).

## Compilare

Pentru a compila proiectul:

```bash
make
```

## Rulare

Exista doua configurari pregatite prin Makefile:

```bash
make run_router0
```

sau

```bash
make run_router1
```

Aceste comenzi pornesc routerul cu fisierele de rutare corespunzatoare.

## Exemple de flux

1. Un pachet IP este primit pe o interfata.
2. Routerul verifica checksum-ul si TTL-ul.
3. Se cauta ruta perfecta in tabela de rutare folosind LPM.
4. Daca next-hop-ul nu este cunoscut, se trimite un ARP Request.
5. Dupa primirea raspunsului ARP, pachetul este trimis mai departe.
6. Daca exista erori (TTL expirat, ruta absenta, pachet corupt), se genereaza un mesaj ICMP de eroare.

## Observatii

Proiectul este o simulare a functionarii unui router de nivel 3 si este util pentru intelegerea fluxului real de forwarding, ARP si ICMP. Este un punct bun de plecare pentru studiul protocoalelor de retea si a conceptelor de dataplane networking.
