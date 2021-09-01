//------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>        //Para manejo de Cadenas
#include <sys/socket.h>    //Para Uso del SocketTCP
#include <netinet/in.h>    //Para Uso del SocketTCP
#include <wiringPi.h>      //para hacer uso de GPIOS
#include <softPwm.h>       //Para hacer uso de PWM de los Servomotores
#include <sys/types.h>     //Para hacer uso función iniDemonio();
#include <sys/stat.h>      //Para hacer uso función iniDemonio();
#include <signal.h>        //Para hacer uso función iniDemonio();
#include <syslog.h>        //Para hacer uso función iniDemonio();
//------------------------------------------------------------------------------------
#define PUERTO 			5000	//Número de puerto asignado al servidor
#define COLA_CLIENTES 	5 		//Tamaño de la cola de espera para clientes
#define TAM_BUFFER 		100
#define MOVERX    23 //Terminal GPIO13
#define MOVERY    26 //Terminal GPIO12
#define EVER   1
#define TIME_DELAY 1000
#define CAMBIO 1
//------------------------------------------------------------------------------------
int iniServidor( );
void moverPanTilt(char direccion);
void initPines(int movx, int movy);
char recibirComando( int cliente_sockfd);
void cambiarBloqueo(char valor);
void cambiarPreferencia(char valor);
void cambiarTiempoMed(char valor);
void iniDemonio();
int movx, movy;
//------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
  int sockfd, cliente_sockfd;
  movx = 14;
  movy = 13;
  char modomov;
  openlog( "Pan&Tilt", LOG_NDELAY | LOG_PID, LOG_LOCAL0 );
  iniDemonio();
  sockfd = iniServidor();
  wiringPiSetup();
  initPines(movx, movy);
  /*
 *  accept - Aceptación de una conexión
 *  Selecciona un cliente de la cola de conexiones establecidas
 *  se crea un nuevo descriptor de socket para el manejon
 *  de la nueva conexion. Apartir de este punto se debe
 *  utilizar el nuevo descriptor de socket
 *  accept - ES BLOQUEANTE
 */
	for(;EVER;)
	{
    syslog(LOG_INFO, "En espera de petición de Conexion...");
    //printf ("En espera de peticiones de conexión ...\n");
    if( (cliente_sockfd = accept(sockfd, NULL, NULL)) < 0 )
    {
      perror("Ocurrio algun problema al atender a un cliente");
      exit(1);
    }
    else
    {
      //printf("Pan&Tilt iniciado \n");
      //printf("Movimiento Bloqueado\n");
      modomov = recibirComando( cliente_sockfd);
      syslog(LOG_INFO, "Comando Recibido:(%c)", modomov);
      //printf("\t Comando recibido: (%c)\n",modomov);
      if(modomov == 'e')
      {
        //printf("Movimiento Desbloqueado\n");
        //printf("Ingresar direccion U=Arriba, D=Abajo, R=Derecha, L=Isquierda, C=Centro \n");
        cambiarBloqueo('e'); //bloquear archivo
	syslog(LOG_INFO, "Alertas Bloqueadas");
      }
      else if(modomov == 'b')
      {
        // printf("Movimiento Bloqueado\n");
        // tomar foto con raspistill
	system("sudo /etc/init.d/motion stop");
	system("raspistill -n -t 500 -e bmp -w 640 -h 480 -o /home/pi/fotosCamara/fotoBase.bmp");
	system("sudo /etc/init.d/motion start");
        cambiarBloqueo('d'); //bloquear archivo
	syslog(LOG_INFO, "Alertas Activadas y FotoBase Tomada");
      }
      else if(modomov == 'o' || modomov == 'i' || modomov == 'n')
      {
        cambiarPreferencia(modomov);
	syslog(LOG_INFO, "Preferencia: %c", modomov);
      }
      else if(modomov == 'q' || modomov == 'w' || modomov == 't' || modomov =='y')
      {
        cambiarTiempoMed(modomov);
	syslog(LOG_INFO, "Tiempo de medicion: %c", modomov);
	syslog(LOG_INFO, "(q=.25s w=.5s t=1s y=2s)");
      }
      else
      {
        //printf("\t Movimiento recibido:%c!\n",modomov);
	syslog(LOG_INFO, "Movimiento Recibido");
        moverPanTilt(modomov);
	syslog(LOG_INFO, "MOVX=%d, MOVY=%d", movx, movy);
 	//printf("\nMOVX= %d, MOVY= %d\n", movx, movy);
      }
      close(cliente_sockfd);
		}
	}
  //Cierre de las conexiones
  close (sockfd);
 closelog();
	return 0;
}
//------------------------------------------------------------------------------------
char recibirComando( int cliente_sockfd)
{
  char mensaje[1];
  char comando;
   if (read (cliente_sockfd, mensaje, 1) < 0)
   {
      perror ("Ocurrio algun problema al recibir el Comando");
      exit(1);
   }
   else
   {
  		comando = mensaje[0];
	  	//printf("Comando: %c",comando);
      //printf("Se recibio mensaje: %s \n",mensaje);
  		return comando;
   }
}

void cambiarBloqueo(char valor)
{
	FILE *apArch;
	apArch = fopen("/home/pi/Variables/varBloqueo.txt", "w");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fprintf(apArch, "%c",valor);
	fclose(apArch);
}

void cambiarPreferencia(char valor)
{
	FILE *apArch;
	apArch = fopen("/home/pi/Variables/varPreferencias.txt", "w");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fprintf(apArch, "%c",valor);
	fclose(apArch);
}

void cambiarTiempoMed(char valor)
{
	FILE *apArch;
	apArch = fopen("/home/pi/Variables/varTiempoMed.txt", "w");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fprintf(apArch, "%c",valor);
	fclose(apArch);
}

//------------------------------------------------------------------------------------
int iniServidor( )
{
   struct sockaddr_in direccion_servidor; //Estructura de la familia AF_INET, que almacena direccion
   int sockfd;
/*
 * AF_INET - Protocolo de internet IPV4
 *  htons - El ordenamiento de bytes en la red es siempre big-endian, por lo que
 *  en arquitecturas little-endian se deben revertir los bytes
 *  INADDR_ANY - Se elige cualquier interfaz de entrada
 */
   memset (&direccion_servidor, 0, sizeof (direccion_servidor));  //se limpia la estructura con ceros
   direccion_servidor.sin_family       = AF_INET;
   direccion_servidor.sin_port      = htons(PUERTO);
   direccion_servidor.sin_addr.s_addr  = INADDR_ANY;

/*
 * Creacion de las estructuras necesarias para el manejo de un socket
 *  SOCK_STREAM - Protocolo orientado a conexión
 */
   printf("Creando Socket ....\n");
   if( (sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0 )
   {
      perror("Ocurrio un problema en la creacion del socket");
      exit(1);
   }

/*
 *  bind - Se utiliza para unir un socket con una dirección de red determinada
 */
   printf("Configurando socket ...\n");
   if( bind(sockfd, (struct sockaddr *) &direccion_servidor, sizeof(direccion_servidor)) < 0 )
   {
      perror ("Ocurrio un problema al configurar el socket");
      exit(1);
   }

/*
 *  listen - Marca al socket indicado por sockfd como un socket pasivo, esto es, como un socket
 *  que será usado para aceptar solicitudes de conexiones de entrada usando "accept"
 *  Habilita una cola asociada la socket para alojar peticiones de conector procedentes
 *  de los procesos clientes
 */
   printf ("Estableciendo la aceptacion de clientes...\n");
   if( listen(sockfd, COLA_CLIENTES) < 0 )
   {
      perror("Ocurrio un problema al crear la cola de aceptar peticiones de los clientes");
      exit(1);
   }
   syslog(LOG_INFO, "Servidor Iniciado");

   return sockfd;
}
//------------------------------------------------------------------------------------
void initPines(int movx, int movy)
{
   //PAN&TILT:
   //Definimos los pines como salidas.
   pinMode (MOVERY, OUTPUT) ;
   pinMode (MOVERX, OUTPUT) ;
   //Creamos señal PWM.
   softPwmCreate(MOVERX, 0, 6400);
   softPwmCreate(MOVERY, 0, 6400);
   //Colocamos los servos centrados como posicion inicial.
   softPwmWrite(MOVERY, movy);
   softPwmWrite(MOVERX, movx);
   delay(1000);
   softPwmWrite(MOVERY, 0);
   softPwmWrite(MOVERX, 0);
   //printf("Pines iniciados \n");
   syslog(LOG_INFO, "Pines Iniciados");
}
void moverPanTilt(char direccion)
{
   switch(direccion)
   {
      case 'd':   //El Pan&Tilt se mueve hacia abajo.
            movy = movy + CAMBIO;
            if(movy <=16)
            {
               softPwmWrite(MOVERY, movy);
               delay(TIME_DELAY);
               softPwmWrite(MOVERY, 0);
            }
            else
               movy = movy - CAMBIO;
         break;
      case 'u':   //El Pan&Tilt se mueve hacia arriba.
            movy = movy - CAMBIO;
            if(movy >=5)
            {
               softPwmWrite(MOVERY, movy);
               delay(TIME_DELAY);
               softPwmWrite(MOVERY, 0);
            }
            else
               movy = movy + CAMBIO;
         break;
      case 'l':   //El Pan&Tilt se mueve hacia la izquierda.
            movx = movx + CAMBIO;
            if(movx <=24)
            {
               softPwmWrite(MOVERX, movx);
               delay(TIME_DELAY);
               softPwmWrite(MOVERX, 0);
            }
            else
               movx = movx - CAMBIO;
         break;
      case 'r':   //El Pan&Tilt se mueve hacia la derecha.
            movx = movx - CAMBIO;
            if(movx >=3)
            {
               softPwmWrite(MOVERX, movx);
               delay(TIME_DELAY);
               softPwmWrite(MOVERX, 0);
            }
            else
               movx = movx + CAMBIO;
         break;
      case 'c':   //El Pan&Tilt se mueve a su punto centrado.
            movx = 14;
            movy = 13;
            softPwmWrite(MOVERX, movx);
            softPwmWrite(MOVERY, movy);
            delay(TIME_DELAY);
            softPwmWrite(MOVERX, 0);
            softPwmWrite(MOVERY, 0);
         break;
      default:
            if ((direccion != 10))
            //printf("Comando no valido: %c \n", direccion);
       	    syslog(LOG_INFO, "Movimiento no valido: %d", direccion);
         break;
      }
}
//------------------------------------------------------------------------------------
void iniDemonio()
{
    FILE *apArch;
    pid_t pid = 0;
    pid_t sid = 0; 
// Se crea el proceso hijo
    pid = fork();
    if( pid == -1 )
    {
        perror("Error al crear el primer proceso hijo\n");
        exit(EXIT_FAILURE);
    }
/*
 * Se termina Proceso padre.
 * Al finalizar el proceso padre el proceso hijo es adoptado por init. 
 * El resultado es que la shell piensa que el comando terminó con éxito, 
 * permitiendo que el proceso hijo se ejecute de manera independiente en segundo plano.
 */
    if( pid )
    {
        printf("Se termina proceso padre, PID del proceso hijo %d \n", pid);
        exit(0);
    }
/* Se restablece el modo de archivo
 * Todos los procesos tiene una máscara que indica que permisos no deben establecerse al crear nuevos archivos. 
 * Así cuando se utilizan llamadas al sistema como open() los permisos especificados se comparan con esta máscara, 
 * desactivando de manera efectiva los que en ella se indiquen.
 * La máscara —denominada umask()— es heredada de padres a hijos por los procesos, por lo que su valor por defecto 
 * será el mismo que el que tenía configurada la shell que lanzó el demonio. Esto significa que el demonio no sabe 
 * que permisos acabarán tenido los archivos que intente crear. Para evitarlo simplemente podemos autorizar todos 
 * los permisos 
 */
    umask(0);
/*
 * se inicia una nueva sesion
 * Cada proceso es miembro de un grupo y estos a su vez se reúnen en sesiones. En cada una de estas hay un proceso 
 * que hace las veces de líder, de tal forma que si muere todos los procesos de la sesión reciben una señal SIGHUP.
 * La idea es que el líder muere cuando se quiere dar la sesión por terminada, por lo que mediante SIGHUP se 
 * notifica al resto de procesos esta circunstancia para que puedan terminar ordenadamente.
 * Obviamente no estamos interesados en que el demonio termine cuando la sesión desde la que fue creado finalice, 
 * por lo que necesitamos crear nuestra propia sesión de la que dicho demonio será el líder.
 */
    sid = setsid();
    if( sid < 0 )
    {
        perror("Error al iniciar sesion");
        exit(EXIT_FAILURE);
    }
// Se realiza un segundo fork para separarnos completamente de la sesion del padre
    pid = fork( );
    if( pid == -1 )
    {
        perror("Error al crear el segundo proceso hijo\n");
        exit(EXIT_FAILURE);
    }
    if( pid )
    {
        printf("PID del segundo proceso hijo %d \n", pid);
        apArch = fopen("/home/pi/demoniospid/pantilt.pid", "w");
        fprintf(apArch, "%d", pid);
        fclose(apArch);

        exit(0);
    }
/* 
 * Se cambia el directorio actual por root.
 * Hasta el momento el directorio de trabajo del proceso es el mismo que el de la shell en el momento en el
 * que se ejecutó el comando. Este podría estar dentro de un punto de montaje cualquiera del sistema, por lo
 * que no tenemos garantías de que vaya a seguir estando disponible durante la ejecución del proceso.
 * Por eso es probable que prefiramos cambiar el directorio de trabajo al directorio raíz, ya que podemos
 * estar seguros de que siempre existirá
 */
    chdir("/");
/*
 * Se cierran los flujos de entrada y salida: stdin, stdout, stderr
 * Puesto que un demonio se ejecuta en segundo plano no debe estar conectado a ninguna terminal. 
 * Sin embargo esto plantea la cuestión de cómo indicar condiciones de error, advertencias u otro 
 * tipo de sucesos del programa. Algunos demonios almacenan estos mensajes en archivos específicos 
 * o en su propia base de datos de sucesos. Sin embargo en muchos sistemas existe un servicio especifico 
 * para registrar estos eventos. En lo sistemas basados en UNIX este servicio lo ofrece el demonio Syslog, 
 * al que otros procesos pueden enviar mensajes a través de la función syslog()
 */
    close( STDIN_FILENO  );
   // close( STDOUT_FILENO );
    close( STDERR_FILENO );
}
//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------
