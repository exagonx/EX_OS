/* =============================================================================
 * lib/wifi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I nomi delle cose di wifi_proto.h: difese, stati, motivi di un rifiuto.
 *
 * ! STANNO IN UN FILE A PARTE, NON NELL'INTESTAZIONE, per la stessa ragione di
 * lib/rete.c: una tabella dentro un .h finisce duplicata in ogni programma che
 * lo include, e due copie della stessa tabella prima o poi non dicono piu' la
 * stessa cosa. Qui la usano il comando `wifi` e chi scrivera' un driver
 * wireless: due, che e' il numero da cui conviene condividere.
 * ============================================================================= */

#include "wifi_proto.h"

const char *wifi_auth_nome(int auth)
{
    switch (auth) {
    case WIFI_AUTH_APERTA: return "aperta";
    case WIFI_AUTH_WEP:    return "WEP";
    case WIFI_AUTH_WPA:    return "WPA";
    case WIFI_AUTH_WPA2:   return "WPA2";
    case WIFI_AUTH_WPA3:   return "WPA3";
    /* ! «PIU' NUOVA DI ME» NON E' «SCONOSCIUTA», e la differenza si vede
     * davanti a una rete che non si apre: la prima dice a chi guarda di
     * aggiornare il sistema, la seconda lo manda a cercare un guasto. */
    default:               return "piu' nuova di questo driver";
    }
}

const char *wifi_stato_nome(int stato)
{
    switch (stato) {
    case WIFI_STATO_SPENTA:   return "radio spenta";
    case WIFI_STATO_FERMA:    return "accesa, non agganciata";
    case WIFI_STATO_CERCA:    return "sto cercando";
    case WIFI_STATO_AGGANCIA: return "mi sto agganciando";
    case WIFI_STATO_CHIAVI:   return "scambio le chiavi";
    case WIFI_STATO_CONNESSA: return "connessa";
    case WIFI_STATO_CADUTA:   return "caduta";
    default:                  return "stato ignoto";
    }
}

const char *wifi_perche(int codice)
{
    switch (codice) {
    case 0:                    return "fatto";
    case WIFI_ERR_NON_TROVATA: return "quella rete non si sente";
    case WIFI_ERR_PAROLA:      return "la password non e' quella";
    case WIFI_ERR_AUTH:        return "difesa che questo driver non sa fare";
    case WIFI_ERR_FIRMWARE:    return "la radio non ha il suo firmware";
    case WIFI_ERR_TEMPO:       return "l'access point non ha risposto";
    default:                   return "motivo ignoto";
    }
}
