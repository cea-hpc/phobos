0) Pas fan de la dénomination "token" utilisée dans la notion de "container de requête", je serai plutôt partant d'utiliser la notion de file descripteur de la socket client , c'est plus clair me semble-t-il

1) pas de réponse attendue par le client pour les releases ne comprenant aucune taille écrite supérieure à 0.
(les lectures essentiellement, et peut-être certaines écritures avec erreur ...)
-> ajout par media du flag to_sync dans le proto pour une release
        - flag à false quand le release ne nécessite pas de sync (et donc aucune réponse n'est attendue côté client)
        - flag à true quand le release nécessite un sync
        - (nb : la written size à 0 n'est pas une indication suffisante pour savoir si on doit ou pas faire un sync car on a tjs le pb des fichiers écrits de taille nulls dont il faut tout de même synchroniser les meta-données écrites sur les media)
-> changement du code des clients pour ne pas attendre une telle réponse dans ces cas là
-> changement du code du LRS pour ne pas générer de réponse dans ces cas là

2) to_sync groupé per device
(2.1, 2.2, 2.3  et 2.4 peuvent être fait en parallèle)
2.1) dans le conteneur de requête release to_sync
 -> Dans le LRS, ajout pour le conteneur d'une requête
        - d'un tableau de champ avec chacun
                - un to_sync_status "todo/done/error" (doit être initialisé à todo par défaut),
                - un device
                - un medium (nb, le medium doit être celui qui est dans le device sinon il y a une erreur à renvoyer immédiatement!)
                - une written_size

        - de la taille de ce tableau (le nombre de medium avec le flag to_sync à true dans la requête de release)
        - un code d'erreur stockant la première erreur rencontrée (doit être initialisé à zéro par défaut)
2.2) rajout d'une response queue (elle contiendra dans un premier temps les conteneurs de requêtes de réponse de release avec des to_sync qui sont finies et pour lesquelles on doit envoyer une réponse puis les désalloués)
2.3) rajout du flag to_sync par device à côté du flag ongoing
2.4) rajout d'une to_sync queue per device qui stocke
        - une queue des conteneurs de requête de release avec un sync à faire sur le media "mounted" dans ce device
        - temps de la plus ancienne requête en to_sync
        - taille nécessaire en to_sync
        - nombre de requêtes en to_sync (taille de la queue qui suit juste après, on peut donc ne pas ajouter ce compteur si l'info est présente dans cette queue)

2.5) Refonte du thread principal du LRS pour utiliser les queues to_sync par device
a) le LRS récupère les messages : rien à changer pour le moment dans le mécanisme de réception des messages. Remplissage, par scheduler de la queue release et de la queue des read_alloc/write_alloc/format
b) (on est dans sched_responses_get) le LRS fait d'abord les releases
 -> si la release à un flag to_sync à true pour l'un de ces devices/media
    alors elle est enregistrée dans les to_sync-queue par device correspondant avec ajout d'une requête et d'une taille correspondante dans les stats de cette to_sync queue et mise à jour du temps si c'est la première dans la queue
 -> on passe tous les ongoing à false des devices de tous medias ciblés (avec ou sans to_sync)
c) Puis par devices, il s'inquiète de savoir si une opération de sync est nécessaire :
 -> si un seuil temps/requête/taille est dépassé, on passe le flag to_sync de la device à true
d) Puis par device, si le on-going io est à false et le to_sync à true, alors il réalise un sync
 -> opération de sync sur le device
 -> si erreur: on passe en failed le DSS adm_status du media et du device
 -> si succès: on ajoute la taille écrite dans le media sur le DSS
 -> pour chaque conteneur de requête de la queue to_sync
        - on l'enlève de la queue local to_sync du device
        - si erreur (on est encore en séquentiel ici avec un unique thread, il ne peut y avoir qu'une unique erreur "à la fois")
                - on créé le conteneur de réponse avec le code d'erreur reçu et on le pousse dans la response_queue
                - pour les autres devices/media en to_sync de cette requête
                        - ceux qui ont encore un statut "todo": on enlève la requête de leur queue locale
                        - ceux qui on un statut "done": on met à jour la written_size du media sur le DSS en diminuant la taille indiquée
                - on desalloue le conteneur de requête
        - si succès:
                - on marque à done le statut de ce device/media dans le conteneur de requête
                - si tous les statuts sont à done
                        - on construit le conteneur de réponse et on le pousse dans la response queue
                        - on désalloue le conteneur de requête
 -> on remet à nul tous les compteurs de la file to_sync : written_size, nb_requête, date butoire
 -> le statut to_sync est mis à false sur le device
e) inspection de la response queue pour envoyer les réponses nécessaires
f) Puis dans le traitement des requêtes :
 -> un device est désormais indisponible pour une nouvelle requête si on a ongoing IO à true OU si on a to_sync à true
 -> Avant le umount, on doit être sûr d'avoir réaliser tous les sync nécessaires, on les réalise donc si nécessaire à ce moment avant le umount (on répète l'étape d) mais ici la condition première est juste d'avoir une queue to_sync non-vide

(3 et 4.1 et 4.2 peuvent être faits en parallèle)
3) rendre paramétrable le déclenchement des to_syncs par famille de device/media
-> ajout du paramètre dans la conf par famille
-> ajout du paramètre dans le scheduler par famille
3.0) rendre paramétrable le seuil de temps
3.1) rendre paramétrable le seuil du nombre de requête
3.2) rendre paramétrable le seuil de la taille écrite

(4.1 et 4.2 peuvent être faits en parallèles)
4) to_sync par le device thread
4.1) rajout d'un thread par device (qui pour l'instant ne fait rien)
  -> rajout d'un thread-ID dans le device
  -> rajout d'un signal (lock + cond) dans le device
  -> lancement de ce thread à la création d'une device
        - chargement initial
        - chargement d'un nouveau device avec notify
  -> pthread_join des threads de tous les devices dans le sched_fini
  -> pour cette étape "préliminaire" 4.0, le thread à un corps à peut prêt vide, il ne fait que la boucle infinie suivante:
        - si running est à false : pthread_exit
        - je dors un peu:
                - lock du signal du device
                - attente avec timer du signal du device, pthread_cond_timedwait (avec sortie sur timeout ou sur signal) (paramètre de la durée du sleep en dur dans le code)
                - unlock du signal du device
4.2) rajout des locks
- Pour les statuts du device (ongoing et to_sync)
- dans la queue to_sync du device
 (on pousse et on retire évidemment en prenant le lock: typiquement le type d'actions regroupées dans une fonction ("prise du lock", "ajout dans la queue", "relache du lock")
- dans la response queue
- dans le conteneur de requête
(les 4.3.1, 4.3.2 , 4.3.3 et 4.3.4 peuvent être faits en parallèle)
4.3) utilisation des verrous par le thread principal (travail préparatoire à la concurrence à venir)
4.3.1) tout accès (test et/ou modif) aux flags du device (ongoing et to_sync) doivent être encadrés par protection du verrou
4.3.2) tout accès aux queues to_sync à l'aide de la prise du verrou
4.3.3) tout accès à la queue response_queue à l'aide d'un verrou
4.3.4) tout accès aux statuts et au code d'erreur d'un conteneur de requête doivent être encadrés par la prise du verrou
4.4) le thread device fait le sync:
- création de l'action faite par ce thread, la boucle infinie devient la suivante
        - si running est à false : pthread_exit
        - on prend le lock sur la to_sync queue
        - on vérifie les éventuels cancel à faire: pour chaque conteneur de requête dans le to_sync
                - si son code d'erreur n'est pas zéro
                        - on enlève ce conteneur de requête de notre queue locale to_sync
                        - on met à jour les compteurs: written_size, nb_requête, et (date cible si on est le plus ancien) de cette queue
                        - on marque notre statut à cancel
        - si on-going io à false et to_sync à true: on réalise le sync
                 -> opération de sync sur le device
                 -> si erreur: on passe en failed le DSS adm_status du media et du device
                 -> si succès: on ajoute la taille écrite dans le media sur le DSS
                 -> pour chaque conteneur de requête de la queue to_sync
                        - on l'enlève de la queue local to_sync du device
                        - on prend le lock sur ce conteneur
                        - on marque à done/error le statut de ce device/media dans le conteneur de requête
                        - si erreur
                                - si le code d'erreur est encore 0
                                        - on le renseigne
                                        - on créé le conteneur de réponse avec le code d'erreur reçu et on le pousse dans la response_queue
                        - si succès:
                                - si le code d'erreur n'est pas zéro, on a râté une erreur qui est arrivé entre temps et qu'on n'a pas "cancelled"
                                        - on enlève la written_size de ce medium sur ce conteneur de requête de pour ce media dans le DSS (on aurait du le cancel, on rattrape le coup ...)
                                        - on passe le device/media statut à canceled dans le conteneur de requête
                                - si le code d'erreur est zéro et si plus aucun statut à undo
                                        - on construit le conteneur de réponse et on le pousse dans la response queue
                        - si tous les statuts plus aucun statut n'est à undo: on désalloue le conteneur de requête
                 -> on remet à nul tous les compteurs de la queue to_sync: written_size, nb requête et date butoire
                 -> le statut to_sync est mis à false sur le device
        - on rend le lock sur la queue to_sync
        - je dors un peu:
                - lock du signal du device
                - attente avec timer du signal du device, pthread_cond_timedwait (avec sortie sur timeout ou sur signal) (paramètre de la durée du sleep en dur dans le code ajusté au temps d'attente cible du to_sync si il existe)
                - unlock du signal du device
- modification du comportement du thread principal
  - on supprime l'étape "d)" (ce n'est plus le thread principal qui fait les sync
  - à l'insertion dans la queue to_sync on signale le device thread
  - pour le umount, le thread principal doit se soucier de réaliser les to_sync nécessaires avant le umount, il doit donc si nécessaire placer le flag to_sync et attendre que celle-ci soit réalisée, par une boucle de test sur le flag to_sync et puis faire le umount

4.5) transfert dans le device thread de la prise de décision du to_sync
- dans le device thread après la vérification des cancel et avant la vérification du sync à faire
        - on regarde si les seuils de déclenchements du to_sync sont dépassés (taille written / nombre de requête / temps max), et si c'est le cas, on passe le flag to_sync à true
- on retire cette étape du thread principal LRS (étape "c)")

(4.6 et 5 peuvent être faits en parallèle)
4.6) rendre paramétrable la durée du sleep par pthread_cond_timedwait d'un device thread
  - ajout d'un champ avec cette valeur dans la queue to_sync d'un device
  - ajout d'une valeur globale dans la conf
  - initialisation de cette valeur à la création du device (initiale + notify)

(5.1 et 5.2 peuvent être faits en parallèle)
5) Format executé dans le thread device
5.1) dans le conteneur d'une requête pour un format:
- ajout d'un statut todo / done / error /canceled pour le medium ciblé avec le device choisi pour faire la requête au moment où le scheduler le choisit

5.2) ajout par device d'une queue scheduled
- un lock protégeant la queue
- une queue de conteneur de requête à effectuer (des read_alloc, des write_alloc, des formats)
- un pointeur sur la requête en question

5.3) On effectue le format par le device thread plutôt que par le thread principal
- Lorsqu'un format arrive à être traité par le thread principal, celui-ci ne fait que choisir le device sur lequel effectué ce format et il ne le réalise pas, il pousse le conteneur de requête dans la queue du device choisi et il signale le device thread
        - nb : pour l'instant lorsqu'un device est choisi pour un formatage, on positionne également son ongoing flag à true. On ne pourra retirer ce ongoing flag que lorsques toutes les requêtes (read_alloc+write_alloc+format) auront été migrées dans la queue scheduled, et donc, c'est le device thread qui gèrera désormais par la gestion de sa file scheduled ce statut ongoing
- On rajoute dans le device thread après la relache du verrou sur la queue des to_sync
        - si on n'est pas en to_sync et qu'on est pas en ongoing (au début cela doit être évident avec le ongoing globale, mais mieux vaut préparer la suite)
                - prise du verrou sur la queue scheduled
                - si une action au moins (c'est à dire pour le moment un format), on la réalise (on ne réalise qu'une action à la fois, pour laisser la place au sync si nécessaire)
                        - on vide l'action de la queue
                - on relache le verrou de la queue
                        - on fait le format (umount/unload/load si nécessaire, format)
                        - on poste le format dans la response queue (prise de lock pour le pousser)
                        - on passe le flag ongoing à false

6) read_alloc par le thread device via la queue scheduled
- idem au format, à la différence prêt que
        - on n'écrit la requête dans la response queue que si on est le dernier à être prêt (tout comme pour le to_sync)
        - on ne passe pas ongoing à false, ça c'est le release futur qui le fera.
        - on rajoute une boucle pour scruter les cancel à faire dans la queue scheduled (sur le modèle de la gestion des cancel dans la queue to_sync)

7) write_alloc par le thread device via la queue scheduled
- idem au read_alloc

8) remplacement du flag ongoing par la prise d'action dans la queue scheduled (avec un nombre maximum par device à fixer), on peut donc avoir plusieurs actions scheduled par device et leur traiteme
-> si on passe à plusieurs actions en cours, il faut garder un compteur pour avoir le nombre de release correspondant
-> on a sans doute également une notion de write/read à maintenir pour éviter par exemple un mélange des deux si le medium ne le permet pas.

9) c'est intéressant de pouvoir compter une opération comme réellement effective dans un device quand effectivement les autres medias sont également prêts , c'est une optimisation qui peut avoir son importance,avoir le flag ongoing IO de chaque device qui passe à true uniquement lorsque la réponse d'allocation part au client (en effet, si le device à fait ses montages et autres, il peut avoir envie de pouvoir faire un sync si il y a des IOs qui ne se font pas car on attend un autre device ...)
-> on peut juger que c'est de l'optim pas forcément nécessaire, à voir ... ce n'est peut-être pas si dur à faire
-> on peut le décaler à plus tard
-> il faut avoir deux états dans un device : l'action est déjà prévue / l'action est ongoing

(10.1 et 10.3 peuvent être fait séparément)
10) séparation des fonctions de communications et de scheduling: séparation du thread principal en un thread de communication et un thread de scheduling
10.1)
- ajout d'un verrou dans les queues des schedulers
        - release queue
        - request queue
10.2)
- ajout d'un id de thread par scheduler
10.3) création du scheduler thread avec aucune action réalisé dedans juste son lancement et sa bonne terminaison
10.4)
- création du communication thread qui reste le thread principal (c'est lui qui a créé les autres et c'est lui qui fini le join à la fin)
        - il test le running et sort proprement
                - ajout du join sur le scheduler thread en plus des devices threads
                - il nettoie la response queue
        - il lit les requêtes entrantes
                - epoll_wait avec un timeout sur les sockets clientes en lecture
                        - les conteneurs des requêtes lues sont poussés dans les releases queues et les requests queue des schedulers (avec prise des verrous associés)
        - il envoie les réponses
                - prise du verrou sur la response queue
                - émission de chacune des réponses
                - libération des conteneurs de réponse
                - relache du verrou sur la response queue
- création du scheduler thread pour chaque scheduler
        - il teste le running et se termine si nécessaire (il nettoie les release et request queues)
        - il traite la release queue (avec prise de lock par rapport au communication thread)
        - il traite la request queue (avec prise de lock par rapport au communication thread)
        - il dort un peu

11) gestion propre du CTRL-C avec attente des IOs en cours
- pour le device thread
        - sur running, on ne sort pas forcément tout de suite,
                - on arrête juste d'exécuter les tâches scheduled
                - on attend les releases des tâches ongoing
                - on réalise les to_sync en attente
                - on exit
- pour le scheduler thread
        - on ne traite plus que les releases tant qu'il reste de devices en statut ongoing ou to_sync
        - puis on exit
- pour le communication thread
        - on n'enqueue plus que les releases tant qu'il reste des devices en statut ongoing ou to_sync
        - puis on attend la fin de tous les threads en vidant la response queue
        - puis on vide la response queue
        - puis on exit

