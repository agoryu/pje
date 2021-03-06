/* --------------------------------------------------------------------------
Suivi d'objets dans une sequence d'images, projet encadre IVI
Copyright (C) 2009  Universite Lille 1

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
Inclure les fichiers d'entete
-------------------------------------------------------------------------- */
#include <stdio.h>
#include <time.h>
#include <limits>
#include <cmath>
#include <cv.h>
#include <highgui.h>
#include "uEye.h"
#include "BlobResult.h" // utilisation de variables blob (biblio Blobslib)
#include "TuioServer.h"
#include "TuioObject.h"
using namespace TUIO; // Necessaire pour utiliser TUIO

/* --------------------------------------------------------------------------
Macros
-------------------------------------------------------------------------- */
// Message d'erreur si necessaire apres une fonction IDS
#define IFIDSERROR(x) if (iRet != IS_SUCCESS) \
{ \
    fprintf (stderr, x " (Code %d)\n", iRet); \
    is_ExitCamera (hCam); \
    return (-1); \
}
#define NB_MAX_BLOBS 1024

float euclideanDist(const CvBox2D& p1, const CvBox2D& p2)
{
    float diffX = (p1.center.x - p2.center.x);
    float diffY = (p1.center.y - p2.center.y);
    return cv::sqrt(diffX*diffX + diffY*diffY);
}

/* --------------------------------------------------------------------------
Programme principal
-------------------------------------------------------------------------- */
int main () {

    // Definir les variables locales
    HIDS hCam = (HIDS) 0; // handle de la camera
    int iRet; // code de retour des fonctions uEye

    // Initialiser la camera
    iRet = is_InitCamera (&hCam, NULL);
    // Succes ?
    if (iRet == IS_SUCCESS && hCam != (HIDS) 0) {

        // Definir les variables locales
        int iSizeX, iSizeY; // Taille d'image
        SENSORINFO sInfo; // Structure pour le format de capteur
        char *pcMemoryImage; // Zone memoire (buffer) d'acquisition
        int iIdImage; // ID du buffer d'acquisition
        int iKey = -1; // Touche
        IplImage *psImageBlobs, *psImageCouleur; // Structures image pour OpenCV
        double dFrameRate; // Cadence d'acquisition (im/s)
        double dExposureTime; // Temps d'exposition (ms)
        int iPixelClock; // Frequence d'horloge pixel (MHz)
        CBlobResult sOldBlobs; // Blobs pour l'analyse de connexite
        CvFont sFont; // Fonte pour l'affichage de commentaires
        int i, j; // Indices de boucle
        CvScalar sColors[NB_MAX_BLOBS];
        int iOldBlobs, iNewBlobs; // Nombres de blobs
        int iNextID = 0; // Indice de la prochaine nouvelle etiquette
        TuioServer *psTuioServer = new TuioServer ();

        /* ------------------------------------------------------------------
        Variables necessitant une initialisation
        ------------------------------------------------------------------ */
        const int iSeuil = 192; // Seuil pour la binarisation
        const unsigned int uiBlobMinSize = 100; // Taille minimale d'un blob
        const unsigned int uiBlobMaxSize = 5000; // Taille maximale d'un blob
        const float fBlobMinRatio = 0.3; // Ratio min pour un blob
        const float dSeuilDistance = 110.0; // Distance max pour association

        /* ------------------------------------------------------------------
        Definir les parametres d'acquisition
        ------------------------------------------------------------------ */
        // Recuperer les informations du capteur de la camera
        is_GetSensorInfo (hCam, &sInfo);
        iSizeX = sInfo.nMaxWidth;
        iSizeY = sInfo.nMaxHeight;
        // Definir le mode d'acquisition (n&b ou couleur)
        iRet = is_SetColorMode (hCam, IS_CM_SENSOR_RAW8);
        IFIDSERROR ("Erreur lors du reglage du type d'image")
        // Definir l'horloge pixel
        iPixelClock = 40;
        iRet = is_PixelClock(hCam, IS_PIXELCLOCK_CMD_SET, (void*)&iPixelClock, sizeof(iPixelClock));
        IFIDSERROR ("Erreur lors du reglage de l'horloge pixel")
        // Definir la cadence d'acquisition
        iRet = is_SetFrameRate (hCam, 10.0, &dFrameRate);
        IFIDSERROR ("Impossible de fixer la cadence d'acquisition")
        printf ("Cadence d'acquisition fixee a %lf images/seconde\n",
            dFrameRate);
        // Definir le temps d'integration
        dExposureTime = 10.0;
        iRet = is_Exposure(hCam, IS_EXPOSURE_CMD_SET_EXPOSURE, &dExposureTime, 8);
        IFIDSERROR ("Erreur lors du reglage du temps d'integration")
        printf ("Temps d'integration fixe a %lf ms\n", dExposureTime);
        // Definir les gains, principal et des composantes couleur
        iRet = is_SetHardwareGain (hCam, 0, 0, 0, 0);
        IFIDSERROR ("Erreur lors du reglage des gains")

        /* ------------------------------------------------------------------
        Allocation memoire pour les images
        ------------------------------------------------------------------ */
        // Creer une image uEye et allocation memoire
        iRet = is_AllocImageMem (hCam, iSizeX, iSizeY, 8, &pcMemoryImage,
            &iIdImage);
        IFIDSERROR ("Erreur d'allocation memoire IDS uEye")
        printf ("Memoire allouee pour le buffer %d, adresse 0x%08X\n",
            iIdImage, (int) pcMemoryImage);
        // Activer cette memoire (l'image acquise va y etre stockee)
        iRet = is_SetImageMem (hCam, pcMemoryImage, iIdImage);
        IFIDSERROR ("Erreur d'association de la memoire")
        // Creer les images openCV
        psImageBlobs = cvCreateImageHeader (cvSize (iSizeX, iSizeY),
            IPL_DEPTH_8U, 1);
        // Les valeurs des pixels sont dans le buffer de la camera IDS
        psImageBlobs->imageData = pcMemoryImage;
        // Creer l'image couleur pour l'etiquetage couleur des blobs
        psImageCouleur = cvCreateImage (cvSize (iSizeX, iSizeY),
            IPL_DEPTH_8U, 3);

        // Creer la fen�tre d'affichage de l'image
        cvNamedWindow ("Acquisition", CV_WINDOW_AUTOSIZE);
        cvInitFont (&sFont, CV_FONT_HERSHEY_SIMPLEX, 0.5, 0.5);
        // Creation d'une "palette" de couleurs aleatoires differentes
        srand (time (NULL) );
        for (i = 0; i < NB_MAX_BLOBS; i++) {
            // Definir les variables locales
            CvScalar sNewColor;
            bool bAlreadyExist = false;
            // Creer les couleurs au hasard
            do {
                sNewColor = CV_RGB (rand () & 255, rand () & 255, rand () & 255);
                j = 0;
                while ( (j < i) && !bAlreadyExist) {
                    bAlreadyExist = sColors[j].val[0] == sNewColor.val[0] &&
                    sColors[j].val[1] == sNewColor.val[1] &&
                    sColors[j].val[2] == sNewColor.val[2];
                    j++;
                }
            }
            while (bAlreadyExist);
            sColors[i] = sNewColor;
        }

        // Boucler pour afficher les images
        while (iKey != 27) {

            // Definir les variables locales
            CBlobResult sNewBlobs; // Blobs pour l'analyse de connexite
            CBlob *psBlob; // Acces a un seul blob
            CvBox2D sEllipse; // Ellipse englobant un blob
            double *dDistance; // Calcul des distances

            // Acquerir l'image
            iRet = is_FreezeVideo (hCam, IS_WAIT);
            IFIDSERROR ("Erreur d'acquisition de l'image")

            // Retourner l'image selon l'axe x
            cvFlip (psImageBlobs, NULL, 1);
            // Binariser l'image
            cvThreshold (psImageBlobs, psImageBlobs, iSeuil, 255,
                CV_THRESH_BINARY);
            // Extraire les blobs dans l'image binarisee
            sNewBlobs = CBlobResult (psImageBlobs, NULL, 0);
            // Eliminer les blobs de surface inf�rieure � un seuil
            sNewBlobs.Filter (sNewBlobs, B_EXCLUDE, CBlobGetArea (),
                B_LESS, uiBlobMinSize);
            sNewBlobs.Filter (sNewBlobs, B_EXCLUDE, CBlobGetArea (),
                B_GREATER, uiBlobMaxSize);
            sNewBlobs.Filter (sNewBlobs, B_EXCLUDE, CBlobGetAxisRatio (),
                B_LESS, fBlobMinRatio);

            // Preparer la mise a jour de la liste TUIO
            psTuioServer->initFrame (TuioTime::getSessionTime ());
            iOldBlobs = sOldBlobs.GetNumBlobs ();
            iNewBlobs = sNewBlobs.GetNumBlobs ();

            // S'il n'y a pas de blobs dans la nouvelle liste et
            // s'il y en avait dans l'ancienne...
            if (!iNewBlobs && iOldBlobs)
            // alors les enlever de la liste TUIO
            for (i = 0; i < iOldBlobs; i++) {
                // Definir les variables locales
                TuioCursor *psTuioCursor;
                // Rechercher le curseur TUIO d'indice ID
                psTuioCursor = psTuioServer->getTuioCursor (
                    sOldBlobs.GetBlob (i)->GetID ());
                psTuioServer->removeExternalTuioCursor (psTuioCursor);
                delete psTuioCursor;
            }
            else {
                // S'il n'y avait de blobs dans l'ancienne liste...
                if (!iOldBlobs) {
                    // Etiquette = numero du blob
                    for (i = 0; i < iNewBlobs; i++) {
                        // Definir les variables locales
                        TuioCursor *psTuioCursor;
                        // Acces au blob dans la liste
                        psBlob = sNewBlobs.GetBlob (i);
                        // Fixer la nouvelle etiquette
                        iNextID++; if (iNextID >= NB_MAX_BLOBS) iNextID = 1;
                        psBlob->SetID (iNextID);
                        // Ellipse englobante et centre du blob
                        sEllipse = psBlob->GetEllipse ();
                        // Ajouter a la liste TUIO
                        psTuioCursor = new TuioCursor (psBlob->GetID (),
                            psBlob->GetID (), sEllipse.center.x,
                            sEllipse.center.y);
                        psTuioServer->addExternalTuioCursor (psTuioCursor);
                    }
                }
                else {
                    /* ------------------------------------------------------
                    Associer les blobs pour le suivi
                    ------------------------------------------------------ */
                    // Allocation memoire pour le stockage
                    dDistance = (double *)malloc (iOldBlobs *
                        iNewBlobs * sizeof (double));
                    /* ------------------------------------------------------
                    Calcul des distances generalisees
                    ------------------------------------------------------ */
                    CBlob oldBlob;
                    CBlob newBlob;
                    for (int i = 0; i < iOldBlobs; i++) {
                        oldBlob = sOldBlobs.GetBlob(i);
                        for (int j = 0; j < iNewBlobs; j++) {
                            newBlob = sNewBlobs.GetBlob(j);
                            dDistance[iNewBlobs * i + j] = euclideanDist(oldBlob.GetEllipse(), newBlob.GetEllipse())
                                                            + abs(cv::sqrt(oldBlob.Area()) - cv::sqrt(newBlob.Area()));
                        }
                    }
                    /* ------------------------------------------------------
                    Calcul du minimum des distances en ligne ou colonne
                    ------------------------------------------------------ */
                    float dMin;
                    int pos;
                    if (iNewBlobs > iOldBlobs) {
                        for(int i=0; i<iOldBlobs; i++) {
                            dMin = std::numeric_limits<float>::infinity();
                            pos = -1;
                            for(int j=0; j<iNewBlobs; j++) {
                                if(dMin > dDistance[iNewBlobs * i + j]) {
                                    dMin = dDistance[iNewBlobs * i + j];
                                    /* les non min sont mis a -1 */
                                    if(pos != -1) {
                                        dDistance[pos] = -1;
                                    }
                                    pos = iNewBlobs * i + j;
                                } else {
                                    dDistance[iNewBlobs * i + j] = -1;
                                }
                            }
                        }
                    }
                    else {
                        for(int j=0; j<iNewBlobs; j++) {
                            dMin = std::numeric_limits<float>::infinity();
                            pos = -1;
                            for(int i=0; i<iOldBlobs; i++) {
                                if(dMin > dDistance[iNewBlobs * i + j]) {
                                    dMin = dDistance[iNewBlobs * i + j];
                                    /* les non min sont mis a -1 */
                                    if(pos != -1) {
                                        dDistance[pos] = -1;
                                    }
                                    pos = iNewBlobs * i + j;
                                } else {
                                    dDistance[iNewBlobs * i + j] = -1;
                                }
                            }
                        }
                    }
                    /* ------------------------------------------------------
                    Elimination des distances min superieures au seuil
                    ------------------------------------------------------ */
                    for (i = 0; i < iOldBlobs * iNewBlobs; i++) {
                        if (dDistance[i] > dSeuilDistance) {
                            dDistance[i] = -1.0;
                        }
                    }
                    // Affichage pour verification
                    for (i = 0; i < iOldBlobs; i++) {
                        for (j = 0; j < iNewBlobs; j++) {
                            printf (" %7.2lf", dDistance[iNewBlobs * i + j]);
                        }
                        printf ("\n");
                    }
                    printf ("\n");
                    /* ------------------------------------------------------
                    Enlever les anciens blobs non associes de la liste TUIO
                    ------------------------------------------------------ */
                    for (i = 0; i < iOldBlobs; i++) {
                        bool valeurValide = false;
                        for (int j=0 ; j < iNewBlobs ; j++)
                        {
                            if (dDistance[iNewBlobs * i + j] != -1)
                            {
                                valeurValide = true;
                                break;
                            }
                        }

                        if (!valeurValide)
                        {
                            TuioCursor *psTuioCursor;
                            // Rechercher le curseur TUIO d'indice ID
                            psTuioCursor = psTuioServer->getTuioCursor (
                                sOldBlobs.GetBlob (i)->GetID ());
                            psTuioServer->removeExternalTuioCursor (psTuioCursor);
                            delete psTuioCursor;
                        }
                    }
                    /* ------------------------------------------------------
                    Balayer la liste des nouveaux blobs
                    ------------------------------------------------------ */
                    for (int j = 0; j < iNewBlobs; j++) {
                        double dMaxColonne = -1.0;
                        int iPosMaxColonne = -1;

                        for (int i=0 ; i < iOldBlobs ; i++)
                        {
                            if (dDistance[iNewBlobs * i + j] > dMaxColonne)
                            {
                                dMaxColonne = dDistance[iNewBlobs * i + j];
                                iPosMaxColonne = i;
                            }
                        }

                        // S'il existe une distance mini dans la colonne
                        if (dMaxColonne > -1.0) {
                            sNewBlobs.GetBlob(j)->SetID(sOldBlobs.GetBlob(iPosMaxColonne)->GetID());
                            TuioCursor *psTuioCursor;
                            // Rechercher le curseur TUIO d'indice ID
                            psTuioCursor = psTuioServer->getTuioCursor (
                                sNewBlobs.GetBlob (j)->GetID ());
                            psTuioServer->updateTuioCursor(psTuioCursor, sNewBlobs.GetBlob(j)->GetEllipse().center.x, sNewBlobs.GetBlob(j)->GetEllipse().center.y);
                            delete psTuioCursor;
                        }
                        // Sinon...
                        else {
                            // Definir une nouvelle etiquette pour le blob
                            TuioCursor *psTuioCursor;
                            // Acces au blob dans la liste
                            CBlob *psBlob = sNewBlobs.GetBlob (i);
                            // Fixer la nouvelle etiquette
                            iNextID++; if (iNextID >= NB_MAX_BLOBS) iNextID = 1;
                            psBlob->SetID (iNextID);
                            // Ellipse englobante et centre du blob
                            CvBox2D sEllipse = psBlob->GetEllipse ();
                            // Ajouter a la liste TUIO
                            psTuioCursor = new TuioCursor (psBlob->GetID (),
                                psBlob->GetID (), sEllipse.center.x,
                                sEllipse.center.y);
                            psTuioServer->addExternalTuioCursor (psTuioCursor);
                        }
                    }
                    /* ------------------------------------------------------
                    Liberer la memoire des distances
                    ------------------------------------------------------ */
                    free (dDistance);
                }
            }

            // Generer une image couleur a partir de 3 plans niveaux de gris
            cvMerge (psImageBlobs, psImageBlobs, psImageBlobs, NULL,
                psImageCouleur);
            // Afficher les caracteristiques des blobs extraits
            for (i = 0; i < sNewBlobs.GetNumBlobs (); i++) {

                // Definir les variables locales
                char pcEtiquette[6];

                // Acces au blob dans la liste
                psBlob = sNewBlobs.GetBlob (i);
                // Ellipse englobante et centre du blob
                sEllipse = psBlob->GetEllipse ();
                // Remplissage du blob avec une couleur propre
                psBlob->FillBlob (psImageCouleur, sColors[psBlob->GetID ()]);
                // Trace d'un point au centre du blob
                cvLine (psImageCouleur, cvPoint ( (int) sEllipse.center.x,
                        (int) sEllipse.center.y),
                    cvPoint ( (int) sEllipse.center.x, (int) sEllipse.center.y),
                    CV_RGB (0, 150, 0), 4, 8, 0);
                // Etiquetage du blob avec son ID
                sprintf (pcEtiquette, "%d", psBlob->GetID ());
                cvPutText (psImageCouleur, pcEtiquette,
                    cvPointFrom32f (sEllipse.center),
                    &sFont, CV_RGB (255, 255, 255));
                // Affichage de l'ellipse des contours de chaque blob
                cvEllipse (psImageCouleur, cvPointFrom32f (sEllipse.center),
                    cvSize ( (int) sEllipse.size.width, (int) sEllipse.size.height),
                    sEllipse.angle, 0, 360, CV_RGB (255, 0, 0));
            }

            // Envoyer la mise a jour TUIO
            psTuioServer->commitFrame ();
            // Copier les nouveaux blobs dans les anciens
            sOldBlobs = sNewBlobs;
            // Afficher l'image
            cvShowImage ("Acquisition", psImageCouleur);
            // Attendre 20 ms que l'utilisateur appuie sur une touche
            iKey = cvWaitKey (20);
        }

        // Liberer la memoire et finir sans erreur
        cvDestroyWindow ("Acquisition");
        psImageBlobs->imageData = NULL;
        cvReleaseImageHeader (&psImageBlobs);
        cvReleaseImage (&psImageCouleur);
        is_FreeImageMem (hCam, pcMemoryImage, iIdImage);
        delete psTuioServer;
        is_ExitCamera (hCam);
        return (0);
    }

    // Erreur lors de l'ouverture de la camera IDS
    else {
        // Afficher un message d'erreur
        printf ("Erreur lors de l'ouverture de la camera IDS uEye" \
            " (verifier la connexion sur l'USB)");
        return (-1);
    }
}
